#pragma once

#include "croftech/sim/BehaviourTree.h"

#include <cstdint>
#include <span>
#include <vector>

namespace ct {

// Changes to a behaviour tree as the Behaviours window makes them, each keeping the node ranges
// and memory slots right. The trees are drafts (BehaviourReading::Draft): a change may leave a
// composite with nothing under it, which behaviourProblems reports, but never puts anything
// under a condition or an action.

inline constexpr std::uint32_t kNoBehaviourParent = 0xFFFFFFFF;

// Composites and decorators.
bool takesChildren(BehaviourNodeKind kind);
// Each node's parent; the root's is kNoBehaviourParent.
std::vector<std::uint32_t> behaviourParents(const BehaviourTree& tree);
std::vector<std::uint32_t> behaviourChildren(const BehaviourTree& tree, std::uint32_t node);
// Whether node is ancestor or under it.
bool isUnder(const BehaviourTree& tree, std::uint32_t node, std::uint32_t ancestor);

// The node and everything under it, as a tree of its own.
BehaviourTree behaviourSubtree(const BehaviourTree& tree, std::uint32_t node);
// Puts the subtree under parent (which takes children) as its child at position, or last if
// position is past its children. Returns where the subtree's root went.
std::uint32_t insertBehaviourSubtree(BehaviourTree& tree, std::uint32_t parent,
                                     std::uint32_t position, const BehaviourTree& subtree);
// Takes the node and everything under it away. Not the root.
void removeBehaviourSubtree(BehaviourTree& tree, std::uint32_t node);
// Moves the node, with everything under it, to be parent's child at position among its children
// once moved. The parent can't be under the node. Returns where the node went.
std::uint32_t moveBehaviourSubtree(BehaviourTree& tree, std::uint32_t node, std::uint32_t parent,
                                   std::uint32_t position);
// Puts a new node of the kind (which takes children) in the node's place, with the node under
// it. The new node is at the node's old index.
void wrapBehaviourNode(BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind);
// Whether the node can be taken away with its children taking its place: under a composite, or
// with at most one child under a decorator, or as the root with one child.
bool canUnwrapBehaviourNode(const BehaviourTree& tree, std::uint32_t node);
void unwrapBehaviourNode(BehaviourTree& tree, std::uint32_t node);
// Changes the node to another kind that takes children if this one does (or doesn't if it
// doesn't), with that kind's default setting.
bool canChangeBehaviourKind(const BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind);
void changeBehaviourKind(BehaviourTree& tree, std::uint32_t node, BehaviourNodeKind kind);

// A node's place in the graph: its top-left corner and its size.
struct BehaviourBox {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Lays the tree out left to right, as its file reads top to bottom: a column for each depth, as
// wide as its widest node, and each node's children stacked in order to its right, the node
// level with their middle. Takes each node's size (sizes[i].width and height) and returns its
// box, with the first column and the top of the tree at the origin.
std::vector<BehaviourBox> layoutBehaviourTree(const BehaviourTree& tree,
                                              std::span<const BehaviourBox> sizes, float columnGap,
                                              float rowGap);
// Where among parent's children a node dropped with its middle at height y goes: after each of
// the others whose middle is above it. The node may be one of them (being reordered) or not
// (joining).
std::uint32_t behaviourDropPosition(const BehaviourTree& tree, std::span<const BehaviourBox> boxes,
                                    std::uint32_t parent, std::uint32_t node, float y);

} // namespace ct
