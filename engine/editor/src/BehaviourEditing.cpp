#include "croftech/editor/BehaviourEditing.h"

#include "croftech/core/Assert.h"

#include <algorithm>
#include <iterator>

namespace ct {
namespace {

// A tree as its file has it: each node with its depth.
struct OutlineNode {
    std::uint32_t depth = 0;
    BehaviourNode node;
};

std::vector<OutlineNode> outlineOf(const BehaviourTree& tree, std::uint32_t first,
                                   std::uint32_t end) {
    std::vector<OutlineNode> outline;
    outline.reserve(end - first);
    std::vector<std::uint32_t> ends; // of the nodes this one is under, from first
    for (std::uint32_t i = first; i < end; ++i) {
        while (!ends.empty() && ends.back() <= i) {
            ends.pop_back();
        }
        outline.push_back({static_cast<std::uint32_t>(ends.size()), tree.nodes[i]});
        ends.push_back(tree.nodes[i].end);
    }
    return outline;
}

std::vector<OutlineNode> outlineOf(const BehaviourTree& tree) {
    return outlineOf(tree, 0, static_cast<std::uint32_t>(tree.nodes.size()));
}

// The nodes again, with their ranges from the depths and their memory slots numbered afresh.
void rebuild(BehaviourTree& tree, std::vector<OutlineNode> outline) {
    tree.nodes.clear();
    tree.nodes.reserve(outline.size());
    std::vector<std::uint32_t> open; // nodes whose range is still growing
    for (OutlineNode& line : outline) {
        while (open.size() > line.depth) {
            tree.nodes[open.back()].end = static_cast<std::uint16_t>(tree.nodes.size());
            open.pop_back();
        }
        open.push_back(static_cast<std::uint32_t>(tree.nodes.size()));
        tree.nodes.push_back(std::move(line.node));
    }
    while (!open.empty()) {
        tree.nodes[open.back()].end = static_cast<std::uint16_t>(tree.nodes.size());
        open.pop_back();
    }
    numberBehaviourMemory(tree);
}

std::uint32_t depthOf(const BehaviourTree& tree, std::uint32_t node) {
    std::uint32_t depth = 0;
    for (std::uint32_t i = 0; i < node; ++i) {
        if (tree.nodes[i].end > node) {
            ++depth; // every node before it whose range holds it is above it
        }
    }
    return depth;
}

} // namespace

bool takesChildren(BehaviourNodeKind kind) {
    const BehaviourCategory category = behaviourNodeInfo(kind).category;
    return category == BehaviourCategory::Composite || category == BehaviourCategory::Decorator;
}

std::vector<std::uint32_t> behaviourParents(const BehaviourTree& tree) {
    std::vector<std::uint32_t> parents(tree.nodes.size(), kNoBehaviourParent);
    for (std::uint32_t i = 0; i < tree.nodes.size(); ++i) {
        for (std::uint32_t child = i + 1; child < tree.nodes[i].end;
             child = tree.nodes[child].end) {
            parents[child] = i;
        }
    }
    return parents;
}

std::vector<std::uint32_t> behaviourChildren(const BehaviourTree& tree, std::uint32_t node) {
    std::vector<std::uint32_t> children;
    for (std::uint32_t child = node + 1; child < tree.nodes[node].end;
         child = tree.nodes[child].end) {
        children.push_back(child);
    }
    return children;
}

bool isUnder(const BehaviourTree& tree, std::uint32_t node, std::uint32_t ancestor) {
    return node >= ancestor && node < tree.nodes[ancestor].end;
}

BehaviourTree behaviourSubtree(const BehaviourTree& tree, std::uint32_t node) {
    BehaviourTree subtree;
    subtree.name = tree.name;
    rebuild(subtree, outlineOf(tree, node, tree.nodes[node].end));
    return subtree;
}

std::uint32_t insertBehaviourSubtree(BehaviourTree& tree, std::uint32_t parent,
                                     std::uint32_t position, const BehaviourTree& subtree) {
    CT_ASSERT(takesChildren(tree.nodes[parent].kind) && !subtree.nodes.empty());
    const std::vector<std::uint32_t> children = behaviourChildren(tree, parent);
    const std::uint32_t at =
        position < children.size() ? children[position] : tree.nodes[parent].end;
    const std::uint32_t depth = depthOf(tree, parent) + 1;
    std::vector<OutlineNode> outline = outlineOf(tree);
    std::vector<OutlineNode> inserted = outlineOf(subtree);
    for (OutlineNode& line : inserted) {
        line.depth += depth;
    }
    outline.insert(outline.begin() + at, std::make_move_iterator(inserted.begin()),
                   std::make_move_iterator(inserted.end()));
    rebuild(tree, std::move(outline));
    return at;
}

void removeBehaviourSubtree(BehaviourTree& tree, std::uint32_t node) {
    CT_ASSERT(node > 0 && node < tree.nodes.size());
    std::vector<OutlineNode> outline = outlineOf(tree);
    outline.erase(outline.begin() + node, outline.begin() + tree.nodes[node].end);
    rebuild(tree, std::move(outline));
}

std::uint32_t moveBehaviourSubtree(BehaviourTree& tree, std::uint32_t node, std::uint32_t parent,
                                   std::uint32_t position) {
    CT_ASSERT(node > 0 && !isUnder(tree, parent, node));
    const BehaviourTree moved = behaviourSubtree(tree, node);
    const std::uint32_t size = tree.nodes[node].end - node;
    removeBehaviourSubtree(tree, node);
    return insertBehaviourSubtree(tree, parent > node ? parent - size : parent, position, moved);
}

void wrapBehaviourNode(BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind) {
    CT_ASSERT(takesChildren(kind));
    std::vector<OutlineNode> outline = outlineOf(tree);
    for (std::uint32_t i = node; i < tree.nodes[node].end; ++i) {
        ++outline[i].depth;
    }
    OutlineNode wrapper{outline[node].depth - 1, {}};
    wrapper.node.kind = kind;
    wrapper.node.value = behaviourNodeInfo(kind).parameter.defaultValue;
    // A note of the root's is about the whole tree, so it stays at the top.
    if (node == 0) {
        wrapper.node.note = std::move(outline[0].node.note);
        outline[0].node.note.clear();
    }
    outline.insert(outline.begin() + node, std::move(wrapper));
    rebuild(tree, std::move(outline));
}

bool canUnwrapBehaviourNode(const BehaviourTree& tree, std::uint32_t node) {
    const std::size_t children = behaviourChildren(tree, node).size();
    if (node == 0) {
        return children == 1;
    }
    const std::uint32_t parent = behaviourParents(tree)[node];
    return behaviourNodeInfo(tree.nodes[parent].kind).category == BehaviourCategory::Composite ||
           children <= 1;
}

void unwrapBehaviourNode(BehaviourTree& tree, std::uint32_t node) {
    CT_ASSERT(canUnwrapBehaviourNode(tree, node));
    std::vector<OutlineNode> outline = outlineOf(tree);
    for (std::uint32_t i = node + 1; i < tree.nodes[node].end; ++i) {
        --outline[i].depth;
    }
    // A note of the root's is about the whole tree, so it stays at the top.
    if (node == 0 && outline[1].node.note.empty()) {
        outline[1].node.note = std::move(outline[0].node.note);
    }
    outline.erase(outline.begin() + node);
    rebuild(tree, std::move(outline));
}

bool canChangeBehaviourKind(const BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind) {
    return takesChildren(tree.nodes[node].kind) == takesChildren(kind);
}

void changeBehaviourKind(BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind) {
    CT_ASSERT(canChangeBehaviourKind(tree, node, kind));
    BehaviourNode& changed = tree.nodes[node];
    changed.kind = kind;
    changed.value = behaviourNodeInfo(kind).parameter.defaultValue;
    numberBehaviourMemory(tree);
}

std::vector<BehaviourBox> layoutBehaviourTree(const BehaviourTree& tree,
                                              std::span<const BehaviourBox> sizes, float columnGap,
                                              float rowGap) {
    const std::uint32_t count = static_cast<std::uint32_t>(tree.nodes.size());
    CT_ASSERT(sizes.size() == count);
    std::vector<BehaviourBox> boxes(count);
    if (count == 0) {
        return boxes;
    }
    const std::vector<OutlineNode> outline = outlineOf(tree);
    std::vector<float> columns; // the width of each depth's widest node
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t depth = outline[i].depth;
        if (columns.size() <= depth) {
            columns.resize(depth + 1, 0.0f);
        }
        columns[depth] = std::max(columns[depth], sizes[i].width);
    }
    std::vector<float> columnX(columns.size(), 0.0f);
    for (std::size_t depth = 1; depth < columns.size(); ++depth) {
        columnX[depth] = columnX[depth - 1] + columns[depth - 1] + columnGap;
    }

    // Heights first, from the leaves up: each subtree is as tall as its node or its children
    // stacked, whichever is taller.
    std::vector<float> subtreeHeight(count, 0.0f);
    for (std::uint32_t i = count; i-- > 0;) {
        float stacked = 0.0f;
        for (std::uint32_t child = i + 1; child < tree.nodes[i].end;
             child = tree.nodes[child].end) {
            stacked += (stacked > 0.0f ? rowGap : 0.0f) + subtreeHeight[child];
        }
        subtreeHeight[i] = std::max(sizes[i].height, stacked);
    }
    // Then places, from the root down: each subtree's tops, its node level with the middle of
    // its children.
    std::vector<float> top(count, 0.0f);
    for (std::uint32_t i = 0; i < count; ++i) {
        float stacked = 0.0f;
        for (std::uint32_t child = i + 1; child < tree.nodes[i].end;
             child = tree.nodes[child].end) {
            stacked += (stacked > 0.0f ? rowGap : 0.0f) + subtreeHeight[child];
        }
        const float middle = top[i] + subtreeHeight[i] * 0.5f;
        boxes[i] = {columnX[outline[i].depth], middle - sizes[i].height * 0.5f, sizes[i].width,
                    sizes[i].height};
        float y = middle - stacked * 0.5f;
        for (std::uint32_t child = i + 1; child < tree.nodes[i].end;
             child = tree.nodes[child].end) {
            top[child] = y;
            y += subtreeHeight[child] + rowGap;
        }
    }
    return boxes;
}

std::uint32_t behaviourDropPosition(const BehaviourTree& tree, std::span<const BehaviourBox> boxes,
                                    std::uint32_t parent, std::uint32_t node, float y) {
    std::uint32_t position = 0;
    for (const std::uint32_t child : behaviourChildren(tree, parent)) {
        if (child != node && boxes[child].y + boxes[child].height * 0.5f < y) {
            ++position;
        }
    }
    return position;
}

} // namespace ct
