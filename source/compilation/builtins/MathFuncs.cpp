//------------------------------------------------------------------------------
// MathFuncs.cpp
// Built-in math system functions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/binding/SystemSubroutine.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/SysFuncsDiags.h"

namespace slang::Builtins {

class Clog2Function : public SystemSubroutine {
public:
    Clog2Function() : SystemSubroutine("$clog2", SubroutineKind::Function) {}

    const Type& checkArguments(const BindContext& context, const Args& args, SourceRange range,
                               const Expression*) const final {
        auto& comp = context.getCompilation();
        if (!checkArgCount(context, false, args, range, 1, 1))
            return comp.getErrorType();

        if (!args[0]->type->isIntegral())
            return badArg(context, *args[0]);

        return comp.getIntegerType();
    }

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue v = args[0]->eval(context);
        if (!v)
            return nullptr;

        auto ci = v.integer();
        ci.flattenUnknowns();
        return SVInt(32, clog2(ci), true);
    }
};

class CountBitsFunction : public SystemSubroutine {
public:
    CountBitsFunction() : SystemSubroutine("$countbits", SubroutineKind::Function) {}

    const Type& checkArguments(const BindContext& context, const Args& args, SourceRange range,
                               const Expression*) const final {
        auto& comp = context.getCompilation();
        if (!checkArgCount(context, false, args, range, 2, INT32_MAX))
            return comp.getErrorType();

        if (!args[0]->type->isBitstreamType())
            return badArg(context, *args[0]);

        for (auto arg : args.subspan(1)) {
            if (!arg->type->isIntegral())
                return badArg(context, *arg);
        }

        return comp.getIntType();
    }

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue value = args[0]->eval(context);
        if (!value)
            return nullptr;

        // TODO: should support any bitstream type...
        const SVInt& iv = value.integer();

        // Figure out which bit values we're checking -- the caller can
        // pass any number of arguments; we always take the LSB and compare
        // that against all bits, counting each one that matches.
        //
        // This array tracks which bit values we've already counted: 0, 1, X, or Z
        bool seen[4]{};
        uint64_t count = 0;

        for (auto arg : args.subspan(1)) {
            ConstantValue v = arg->eval(context);
            if (!v)
                return nullptr;

            logic_t bit = v.integer()[0];
            if (bit.value == 0) {
                if (!seen[0]) {
                    count += iv.countZeros();
                    seen[0] = true;
                }
            }
            else if (bit.value == 1) {
                if (!seen[1]) {
                    count += iv.countOnes();
                    seen[1] = true;
                }
            }
            else if (bit.value == logic_t::X_VALUE) {
                if (!seen[2]) {
                    count += iv.countXs();
                    seen[2] = true;
                }
            }
            else if (bit.value == logic_t::Z_VALUE) {
                if (!seen[3]) {
                    count += iv.countZs();
                    seen[3] = true;
                }
            }
        }

        return SVInt(32, count, true);
    }
};

class CountOnesFunction : public SystemSubroutine {
public:
    CountOnesFunction() : SystemSubroutine("$countones", SubroutineKind::Function) {}

    const Type& checkArguments(const BindContext& context, const Args& args, SourceRange range,
                               const Expression*) const final {
        auto& comp = context.getCompilation();
        if (!checkArgCount(context, false, args, range, 1, 1))
            return comp.getErrorType();

        if (!args[0]->type->isBitstreamType())
            return badArg(context, *args[0]);

        return comp.getIntType();
    }

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue value = args[0]->eval(context);
        if (!value)
            return nullptr;

        // TODO: should support any bitstream type...
        const SVInt& iv = value.integer();

        uint64_t count = iv.countOnes();
        return SVInt(32, count, true);
    }
};

class BooleanBitVectorFunction : public SystemSubroutine {
public:
    enum BVFKind { OneHot, OneHot0, IsUnknown };

    BooleanBitVectorFunction(const std::string& name, BVFKind kind) :
        SystemSubroutine(name, SubroutineKind::Function), kind(kind) {}

    const Type& checkArguments(const BindContext& context, const Args& args, SourceRange range,
                               const Expression*) const final {
        auto& comp = context.getCompilation();
        if (!checkArgCount(context, false, args, range, 1, 1))
            return comp.getErrorType();

        if (!args[0]->type->isBitstreamType())
            return badArg(context, *args[0]);

        return comp.getBitType();
    }

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue value = args[0]->eval(context);
        if (!value)
            return nullptr;

        // TODO: should support any bitstream type...
        const SVInt& iv = value.integer();

        switch (kind) {
            case OneHot:
                return SVInt(1, uint64_t(iv.countOnes() == 1), false);
            case OneHot0:
                return SVInt(1, uint64_t(iv.countOnes() <= 1), false);
            case IsUnknown:
                return SVInt(1, uint64_t(iv.hasUnknown()), false);
            default:
                THROW_UNREACHABLE;
        }
    }

private:
    BVFKind kind;
};

template<double Func(double)>
class RealMath1Function : public SimpleSystemSubroutine {
public:
    RealMath1Function(Compilation& comp, const std::string& name) :
        SimpleSystemSubroutine(name, SubroutineKind::Function, 1, { &comp.getRealType() },
                               comp.getRealType(), false) {}

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue v = args[0]->eval(context);
        if (!v)
            return nullptr;

        double result = Func(v.real());
        return real_t(result);
    }
};

template<double Func(double, double)>
class RealMath2Function : public SimpleSystemSubroutine {
public:
    RealMath2Function(Compilation& comp, const std::string& name) :
        SimpleSystemSubroutine(name, SubroutineKind::Function, 2,
                               { &comp.getRealType(), &comp.getRealType() }, comp.getRealType(),
                               false) {}

    ConstantValue eval(EvalContext& context, const Args& args, SourceRange,
                       const CallExpression::SystemCallInfo&) const final {
        ConstantValue a = args[0]->eval(context);
        ConstantValue b = args[1]->eval(context);
        if (!a || !b)
            return nullptr;

        double result = Func(a.real(), b.real());
        return real_t(result);
    }
};

void registerMathFuncs(Compilation& c) {
    c.addSystemSubroutine(std::make_shared<Clog2Function>());
    c.addSystemSubroutine(std::make_shared<CountBitsFunction>());
    c.addSystemSubroutine(std::make_shared<CountOnesFunction>());

#define REGISTER(name, kind) \
    c.addSystemSubroutine(   \
        std::make_shared<BooleanBitVectorFunction>(name, BooleanBitVectorFunction::kind))

    REGISTER("$onehot", OneHot);
    REGISTER("$onehot0", OneHot0);
    REGISTER("$isunknown", IsUnknown);

#undef REGISTER
#define REGISTER(name, func) \
    c.addSystemSubroutine(std::make_shared<RealMath1Function<(func)>>(c, name))

    REGISTER("$ln", std::log);
    REGISTER("$log10", std::log10);
    REGISTER("$exp", std::exp);
    REGISTER("$sqrt", std::sqrt);
    REGISTER("$floor", std::floor);
    REGISTER("$ceil", std::ceil);
    REGISTER("$sin", std::sin);
    REGISTER("$cos", std::cos);
    REGISTER("$tan", std::tan);
    REGISTER("$asin", std::asin);
    REGISTER("$acos", std::acos);
    REGISTER("$atan", std::atan);
    REGISTER("$sinh", std::sinh);
    REGISTER("$cosh", std::cosh);
    REGISTER("$tanh", std::tanh);
    REGISTER("$asinh", std::asinh);
    REGISTER("$acosh", std::acosh);
    REGISTER("$atanh", std::atanh);

#undef REGISTER
#define REGISTER(name, func) \
    c.addSystemSubroutine(std::make_shared<RealMath2Function<(func)>>(c, name))

    REGISTER("$pow", std::pow);
    REGISTER("$atan2", std::atan2);
    REGISTER("$hypot", std::hypot);

#undef REGISTER
}

} // namespace slang::Builtins
