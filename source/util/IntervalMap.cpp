//------------------------------------------------------------------------------
// IntervalMap.cpp
// Specialized map data structure with interval keys
//
// SPDX-FileCopyrightText: Michael Popoloski
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#include "slang/util/IntervalMap.h"

namespace slang {

namespace IntervalMapDetails {

void Path::replaceRoot(void* node, uint32_t size, IndexPair offset) {
    ASSERT(!path.empty());
    path.front() = Entry(node, size, offset.first);
    path.insert(path.begin() + 1, Entry(childAt(0), offset.second));
}

void Path::moveLeft(uint32_t level) {
    ASSERT(level);

    // Go up the tree until we find a node where we can go left.
    uint32_t l = 0;
    if (valid()) {
        l = level - 1;
        while (path[l].offset == 0) {
            ASSERT(l);
            --l;
        }
    }
    else if (height() < level) {
        path.resize(level + 1, Entry(nullptr, 0, 0));
    }

    --path[l].offset;

    // Get the rightmost node of the new subtree.
    NodeRef nodeRef = childAt(l);
    for (++l; l != level; ++l) {
        path[l] = Entry(nodeRef, nodeRef.size() - 1);
        nodeRef = nodeRef.childAt(nodeRef.size() - 1);
    }
    path[l] = Entry(nodeRef, nodeRef.size() - 1);
}

void Path::moveRight(uint32_t level) {
    ASSERT(level);

    // Go up the tree until we find a node where we can go right.
    uint32_t l = level - 1;
    while (l && path[l].offset == path[l].size - 1)
        --l;

    // If we hit the end we've gone as far as we can.
    if (++path[l].offset == path[l].size)
        return;

    NodeRef nodeRef = childAt(l);
    for (++l; l != level; ++l) {
        path[l] = Entry(nodeRef, 0);
        nodeRef = nodeRef.childAt(0);
    }
    path[l] = Entry(nodeRef, 0);
}

IndexPair distribute(uint32_t numNodes, uint32_t numElements, uint32_t capacity, uint32_t* newSizes,
                     uint32_t position, bool grow) {
    ASSERT(numElements + grow <= numNodes * capacity);
    ASSERT(position <= numElements);
    if (!numNodes)
        return {};

    // left-leaning even distribution
    const uint32_t perNode = (numElements + grow) / numNodes;
    const uint32_t extra = (numElements + grow) % numNodes;
    IndexPair posPair(numNodes, 0);
    uint32_t sum = 0;
    for (uint32_t n = 0; n != numNodes; ++n) {
        sum += newSizes[n] = perNode + (n < extra);
        if (posPair.first == numNodes && sum > position)
            posPair = {n, position - (sum - newSizes[n])};
    }

    // Subtract the grow element that was added.
    ASSERT(sum == numElements + grow);
    if (grow) {
        ASSERT(posPair.first < numNodes);
        ASSERT(newSizes[posPair.first]);
        --newSizes[posPair.first];
    }

    return posPair;
}

} // namespace IntervalMapDetails

} // namespace slang
