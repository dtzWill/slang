// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT

#include "Test.h"

#include "slang/diagnostics/DiagnosticClient.h"
#include "slang/diagnostics/TextDiagnosticClient.h"
#include "slang/text/SourceManager.h"

TEST_CASE("Diagnostic Line Number") {
    auto& text = "`include \"foofile\"\nident";

    // Include a file that doesn't exist, we should still parse the identifier
    // on the next line, but have a diagnostic error on line 1
    Token token = lexToken(text);

    CHECK(token.kind == TokenKind::Identifier);
    CHECK(token.valueText() == "ident");
    CHECK(diagnostics.size() == 1);
    std::string message = to_string(diagnostics[0]);
    int line, col;
    sscanf(message.c_str(), "source:%d:%d", &line, &col);
    CHECK(line == 1);
    CHECK(col == 10);
}

TEST_CASE("Diagnostic reporting with `line") {
    auto& text = "`line 100 \"foo.svh\" 0\n"
                 "`include \"foofile\"\n"
                 "ident";

    lexToken(text);
    CHECK(diagnostics.size() == 1);
    std::string message = to_string(diagnostics[0]);
    int line, col;
    int matches = sscanf(message.c_str(), "foo.svh:%d:%d", &line, &col);
    REQUIRE(matches == 2);
    CHECK(line == 100);
    CHECK(col == 10);
}

TEST_CASE("undef errors") {
    // There are errors for attempting to undef a built-in
    // and also for missing the token of what to undef altogether
    // make sure we only give an error about one at a time
    auto& text = "`undef\n";
    Token token = lexToken(text);

    CHECK(token.kind == TokenKind::EndOfFile);

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == diag::ExpectedIdentifier);

    auto& text2 = "`undef __LINE__\n";
    token = lexToken(text2);

    CHECK(token.kind == TokenKind::EndOfFile);

    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == diag::UndefineBuiltinDirective);
}

TEST_CASE("keywords_errors") {
    // verify all the correct errors are generated by the keywords macros
    auto& text = "`begin_keywords \"foo\"\n";

    Token token = lexToken(text);
    CHECK(token.kind == TokenKind::EndOfFile);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == diag::UnrecognizedKeywordVersion);

    auto& text2 = "`begin_keywords\n";

    token = lexToken(text2);
    CHECK(token.kind == TokenKind::EndOfFile);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == diag::ExpectedStringLiteral);

    auto& text3 = "`end_keywords\n";

    token = lexToken(text3);
    CHECK(token.kind == TokenKind::EndOfFile);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code == diag::MismatchedEndKeywordsDirective);
}

TEST_CASE("Diag within macro arg") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO(blah) blah
`define BAR(blah) `FOO(blah)

module m;
    struct { int i; } asdf;
    int i = `BAR(asdf.bar);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:7:23: error: no member named 'bar' in '<unnamed unpacked struct>'
    int i = `BAR(asdf.bar);
                 ~~~~~^~~
)");
}

TEST_CASE("Diag within macro body") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO(blah) blah.bar
`define BAR(blah) `FOO(blah)

module m;
    struct { int i; } asdf;
    int i = `BAR(asdf);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:7:13: error: no member named 'bar' in '<unnamed unpacked struct>'
    int i = `BAR(asdf);
            ^~~~~~~~~~
source:3:19: note: expanded from macro 'BAR'
`define BAR(blah) `FOO(blah)
                  ^~~~~~~~~~
source:2:24: note: expanded from macro 'FOO'
`define FOO(blah) blah.bar
                  ~~~~~^~~
)");
}

TEST_CASE("Diag range within arg and caret within body") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO(blah) blah++
`define BAR(blah) `FOO(blah)

module m;
    struct { int i; } asdf;
    int i;
    initial i = `BAR(asdf);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:8:17: error: invalid operand type '<unnamed unpacked struct>' to unary expression
    initial i = `BAR(asdf);
                ^    ~~~~
source:3:19: note: expanded from macro 'BAR'
`define BAR(blah) `FOO(blah)
                  ^    ~~~~
source:2:23: note: expanded from macro 'FOO'
`define FOO(blah) blah++
                  ~~~~^
)");
}

TEST_CASE("Diag caret within macro arg only") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO(blah) blah
`define BAR(blah) `FOO(blah)

module m;
    int i = `BAR(++);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:6:21: error: expected expression
    int i = `BAR(++);
                    ^
)");
}

TEST_CASE("Diag range split across args") {
    auto tree = SyntaxTree::fromText(R"(
`define BAZ(xy) xy
`define FOO(blah, flurb) blah+`BAZ(flurb)
`define BAR(blah, flurb) `FOO(blah, flurb)

module m;
    struct { int i; } asdf;
    struct { int i; } bar;
    int i = `BAR(asdf, bar);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:9:13: error: invalid operands to binary expression ('<unnamed unpacked struct>' and '<unnamed unpacked struct>')
    int i = `BAR(asdf, bar);
            ^    ~~~~  ~~~
source:4:26: note: expanded from macro 'BAR'
`define BAR(blah, flurb) `FOO(blah, flurb)
                         ^    ~~~~  ~~~~~
source:3:30: note: expanded from macro 'FOO'
`define FOO(blah, flurb) blah+`BAZ(flurb)
                         ~~~~^     ~~~~~
)");
}

TEST_CASE("Diag macro args with split locations") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO(abc) abc
`define BAR(blah, flurb) `FOO(blah + flurb)

module m;
    struct { int i; } asdf;
    struct { int i; } bar;
    int i = `BAR(asdf, bar);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:8:13: error: invalid operands to binary expression ('<unnamed unpacked struct>' and '<unnamed unpacked struct>')
    int i = `BAR(asdf, bar);
            ^    ~~~~  ~~~
source:3:36: note: expanded from macro 'BAR'
`define BAR(blah, flurb) `FOO(blah + flurb)
                              ~~~~ ^ ~~~~~
)");
}

TEST_CASE("Diag macro single range split across macros") {
    auto tree = SyntaxTree::fromText(R"(
`define FOO (i
`define BAR 1)
`define TOP `FOO + `BAR ()

module m;
    int i;
    int j = `TOP;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:8:13: error: expression is not callable
    int j = `TOP;
            ^~~~
source:4:25: note: expanded from macro 'TOP'
`define TOP `FOO + `BAR ()
            ~~~~~~~~~~~ ^
)");
}

TEST_CASE("Diag range within macro arg") {
    auto tree = SyntaxTree::fromText(R"(
`define PASS(asdf, barr) asdf barr

module m;
    int i;
    int j = `PASS(i + 1, ());
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:6:26: error: expression is not callable
    int j = `PASS(i + 1, ());
                      ~  ^
source:2:31: note: expanded from macro 'PASS'
`define PASS(asdf, barr) asdf barr
                         ~~~~ ^
)");
}

TEST_CASE("Multiple ranges split between macro and not") {
    auto tree = SyntaxTree::fromText(R"(
`define PASS(asdf) asdf

module m;
    bit b;
    int j = (b) `PASS([1]);
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:6:24: error: scalar type cannot be indexed
    int j = (b) `PASS([1]);
             ~         ^
source:2:20: note: expanded from macro 'PASS'
`define PASS(asdf) asdf
                   ^~~~
)");
}

TEST_CASE("Diag include stack") {
    auto& sm = SyntaxTree::getDefaultSourceManager();
    sm.assignText("fake-include1.svh", R"(
`include "fake-include2.svh"
)");
    sm.assignText("fake-include2.svh", R"(
i + 1 ()
)");

    auto tree = SyntaxTree::fromText(R"(
module m;
    int i;
    int j =
`include "fake-include1.svh"
    ;
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
in file included from source:5:
in file included from fake-include1.svh:2:
fake-include2.svh:2:7: error: expression is not callable
i + 1 ()
    ~ ^
)");
}

TEST_CASE("Diag include stack -- skipped tokens") {
    SourceManager sm;
    sm.assignText("fake-include1.svh", R"(
`include <asdf
)");
    auto tree = SyntaxTree::fromText(R"(
module m;
`include "fake-include1.svh"
endmodule
)",
                                     sm);

    Compilation compilation;
    compilation.addSyntaxTree(tree);
    compilation.getAllDiagnostics();
}

TEST_CASE("DiagnosticEngine stuff") {
    class TestClient : public DiagnosticClient {
    public:
        int count = 0;
        std::string lastMessage;
        DiagnosticSeverity lastSeverity;

        void report(const ReportedDiagnostic& diag) final {
            count++;
            lastMessage = diag.formattedMessage;
            lastSeverity = diag.severity;
        }
    };

    DiagnosticEngine engine(getSourceManager());
    auto client = std::make_shared<TestClient>();
    engine.addClient(client);

    Diagnostic diag(diag::ExpectedClosingQuote, SourceLocation());
    engine.issue(diag);

    CHECK(client->count == 1);
    CHECK(client->lastMessage == "missing closing quote");
    CHECK(engine.getNumErrors() == 1);
    CHECK(engine.getNumWarnings() == 0);

    engine.setSeverity(diag::ExpectedClosingQuote, DiagnosticSeverity::Warning);
    engine.issue(diag);

    CHECK(client->count == 2);
    CHECK(client->lastMessage == "missing closing quote");
    CHECK(engine.getNumErrors() == 1);
    CHECK(engine.getNumWarnings() == 1);

    engine.setMessage(diag::ExpectedClosingQuote, "foobar");
    engine.issue(diag);

    CHECK(client->count == 3);
    CHECK(client->lastMessage == "foobar");
    CHECK(engine.getNumErrors() == 1);
    CHECK(engine.getNumWarnings() == 2);
    CHECK(engine.getMessage(diag::ExpectedClosingQuote) == "foobar");

    engine.clearMappings();
    CHECK(engine.getMessage(diag::ExpectedClosingQuote) == "missing closing quote");
    CHECK(engine.getSeverity(diag::ExpectedClosingQuote, {}) == DiagnosticSeverity::Error);

    engine.clearCounts();
    CHECK(client->count == 3);
    CHECK(engine.getNumErrors() == 0);
    CHECK(engine.getNumWarnings() == 0);

    engine.clearClients();
    engine.issue(diag);
    CHECK(client->count == 3);

    engine.addClient(client);
    engine.issue(diag);
    CHECK(client->count == 4);

    engine.setSeverity(diag::ExpectedClosingQuote, DiagnosticSeverity::Ignored);
    engine.issue(diag);
    CHECK(client->count == 4);

    engine.setIgnoreAllNotes(true);
    engine.setIgnoreAllWarnings(true);
    engine.setWarningsAsErrors(true);
    engine.setErrorsAsFatal(true);
    engine.setFatalsAsErrors(true);

    diag.code = diag::RealLiteralUnderflow;
    engine.issue(diag);
    CHECK(client->count == 4);

    diag.code = diag::NoteImportedFrom;
    engine.issue(diag);
    CHECK(client->count == 4);

    engine.setIgnoreAllWarnings(false);
    diag.code = diag::RealLiteralUnderflow;
    engine.issue(diag);
    CHECK(client->count == 5);
    CHECK(client->lastSeverity == DiagnosticSeverity::Error);

    diag.code = diag::DotOnType;
    engine.issue(diag);
    CHECK(client->count == 6);
    CHECK(client->lastSeverity == DiagnosticSeverity::Fatal);

    engine.setErrorLimit(7);
    for (int i = 0; i < 10; i++)
        engine.issue(diag);
    CHECK(client->count == 10); // includes 2 warnings and 1 fatal
}

TEST_CASE("DiagnosticEngine::setWarningOptions") {
    auto options = std::vector{
        "everything"s, "none"s,     "error"s, "error=case-gen-dup"s, "no-error=empty-member"s,
        "empty-stmt"s, "no-extra"s, "asdf"s};

    DiagnosticEngine engine(getSourceManager());
    engine.setDefaultWarnings();

    Diagnostics diags = engine.setWarningOptions(options);
    CHECK(diags.size() == 1);

    std::string msg = DiagnosticEngine::reportAll(getSourceManager(), diags);
    CHECK(msg == "warning: unknown warning option '-Wasdf' [-Wunknown-warning-option]\n");
}

TEST_CASE("Diagnostic Pragmas") {
    auto tree = SyntaxTree::fromText(R"(
module m;
    ; // warn
`pragma diagnostic ignore="-Wempty-member"
    ; // hidden
`pragma diagnostic push
    ; // also hidden
`pragma diagnostic error="-Wempty-member"
    ; // error
`pragma diagnostic warn="-Wempty-member"
    ; // warn
`pragma diagnostic pop
`pragma diagnostic pop  // does nothing
    ; // hidden again

`pragma diagnostic fatal="empty-member" // ok to not use -W
`pragma diagnostic ignore=("default", "empty-member")
    ; // ignored
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    DiagnosticEngine engine(tree->sourceManager());
    Diagnostics pragmaDiags = engine.setMappingsFromPragmas();
    CHECK(pragmaDiags.empty());

    auto client = std::make_shared<TextDiagnosticClient>();
    engine.addClient(client);
    for (auto& diag : compilation.getAllDiagnostics())
        engine.issue(diag);

    CHECK("\n"s + client->getString() == R"(
source:3:5: warning: extra ';' has no effect [-Wempty-member]
    ; // warn
    ^
source:9:5: error: extra ';' has no effect [-Wempty-member]
    ; // error
    ^
source:11:5: warning: extra ';' has no effect [-Wempty-member]
    ; // warn
    ^
)");
}

TEST_CASE("Diagnostics with Unicode and tabs in source snippet") {
    auto tree = SyntaxTree::fromText(u8R"(
module m;
    string s = "literal\🍌";
    int 	/* // 꿽꿽꿽꿽꿽꿽꿽 */		갑곯꿽 = "꿽꿽꿽"; // 꿽꿽꿽꿽꿽꿽꿽
endmodule
)");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    std::string check = R"(
source:3:24: warning: unknown character escape sequence '\🍌' [-Wunknown-escape-code]
    string s = "literal\🍌";
                       ^
source:4:42: error: UTF-8 sequence in source text; SystemVerilog identifiers must be ASCII
    int         /* // 꿽꿽꿽꿽꿽꿽꿽 */          갑곯꿽 = "꿽꿽꿽"; // 꿽꿽꿽꿽꿽꿽꿽
                                                 ^
source:4:42: error: expected declarator
    int         /* // 꿽꿽꿽꿽꿽꿽꿽 */          갑곯꿽 = "꿽꿽꿽"; // 꿽꿽꿽꿽꿽꿽꿽
                                                 ^
)";
    CHECK(result == check);
}

TEST_CASE("Diagnostics with invalid UTF8 printed") {
    auto tree = SyntaxTree::fromText("module m;\n"
                                     "    string s = \"literal \xed\xa0\x80\xed\xa0\x80\";\n"
                                     "    int i = /* asdf a\u0308\u0019\U0001057B */ a;\n"
                                     "endmodule\n");

    Compilation compilation;
    compilation.addSyntaxTree(tree);

    auto& diagnostics = compilation.getAllDiagnostics();
    std::string result = "\n" + report(diagnostics);
    CHECK(result == R"(
source:2:25: warning: invalid UTF-8 sequence in source text [-Winvalid-source-encoding]
    string s = "literal <ED><A0><80><ED><A0><80>";
                        ^
source:3:33: error: use of undeclared identifier 'a'
    int i = /* asdf ä<U+19><U+1057B> */ a;
                                        ^
)");
}
