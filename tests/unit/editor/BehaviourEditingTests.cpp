#include "croftech/editor/BehaviourEditing.h"

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ct::BehaviourNodeKind;

ct::BehaviourTree draft(std::string_view text) {
    std::string error;
    std::optional<ct::BehaviourTree> tree =
        ct::parseBehaviourTree(text, "test", error, ct::BehaviourReading::Draft);
    REQUIRE_MESSAGE(tree.has_value(), error);
    return std::move(*tree);
}

std::string text(const ct::BehaviourTree& tree) { return ct::formatBehaviourTree(tree); }

// Every change leaves a tree that reads back the same from its file.
void checkWhole(const ct::BehaviourTree& tree) { CHECK(draft(text(tree)) == tree); }

constexpr std::string_view kTree = "sequence\n"
                                   "    see\n"
                                   "    fallback\n"
                                   "        chase\n"
                                   "        wait 2\n"
                                   "    cooldown\n"
                                   "        patrol\n";

} // namespace

TEST_CASE("a behaviour tree's parents and children") {
    const ct::BehaviourTree tree = draft(kTree);
    const std::vector<std::uint32_t> parents = ct::behaviourParents(tree);
    CHECK(parents == std::vector<std::uint32_t>{ct::kNoBehaviourParent, 0, 0, 2, 2, 0, 5});
    CHECK(ct::behaviourChildren(tree, 0) == std::vector<std::uint32_t>{1, 2, 5});
    CHECK(ct::behaviourChildren(tree, 1).empty());
    CHECK(ct::isUnder(tree, 4, 2));
    CHECK(ct::isUnder(tree, 2, 2));
    CHECK_FALSE(ct::isUnder(tree, 5, 2));
    CHECK(text(ct::behaviourSubtree(tree, 2)) == "fallback\n    chase\n    wait 2\n");
}

TEST_CASE("subtrees are added, taken away and moved") {
    ct::BehaviourTree tree = draft(kTree);
    const ct::BehaviourTree hesitate = draft("sequence\n    wait 1\n    chase\n");

    CHECK(ct::insertBehaviourSubtree(tree, 2, 1, hesitate) == 4);
    CHECK(text(tree) == "sequence\n"
                        "    see\n"
                        "    fallback\n"
                        "        chase\n"
                        "        sequence\n"
                        "            wait 1\n"
                        "            chase\n"
                        "        wait 2\n"
                        "    cooldown 2\n"
                        "        patrol\n");
    // The waits remember in order, then the cooldown.
    CHECK(tree.nodes[5].memory == 0);
    CHECK(tree.nodes[7].memory == 1);
    CHECK(tree.nodes[8].memory == 2);
    checkWhole(tree);

    // Last, past the end.
    CHECK(ct::insertBehaviourSubtree(tree, 0, 99, draft("hear\n")) == 10);
    CHECK(tree.nodes[0].end == 11);

    // Up the tree: the fallback's sequence becomes the root's first child.
    CHECK(ct::moveBehaviourSubtree(tree, 4, 0, 0) == 1);
    CHECK(text(tree) == "sequence\n"
                        "    sequence\n"
                        "        wait 1\n"
                        "        chase\n"
                        "    see\n"
                        "    fallback\n"
                        "        chase\n"
                        "        wait 2\n"
                        "    cooldown 2\n"
                        "        patrol\n"
                        "    hear\n");
    checkWhole(tree);

    // Down it, into a node after it: the first sequence goes last under the cooldown.
    CHECK(ct::moveBehaviourSubtree(tree, 1, 8, 1) == 7);
    CHECK(text(tree) == "sequence\n"
                        "    see\n"
                        "    fallback\n"
                        "        chase\n"
                        "        wait 2\n"
                        "    cooldown 2\n"
                        "        patrol\n"
                        "        sequence\n"
                        "            wait 1\n"
                        "            chase\n"
                        "    hear\n");
    CHECK(ct::behaviourProblems(tree).size() == 1);
    // Reordered among its own: see goes after the fallback.
    CHECK(ct::moveBehaviourSubtree(tree, 1, 0, 1) == 4);
    CHECK(ct::behaviourChildren(tree, 0) == std::vector<std::uint32_t>{1, 4, 5, 10});

    ct::removeBehaviourSubtree(tree, 5);
    CHECK(text(tree) == "sequence\n"
                        "    fallback\n"
                        "        chase\n"
                        "        wait 2\n"
                        "    see\n"
                        "    hear\n");
    CHECK(tree.nodes[3].memory == 0);
    checkWhole(tree);
}

TEST_CASE("nodes are wrapped, unwrapped and changed") {
    ct::BehaviourTree tree = draft("# The whole tree.\n"
                                   "fallback\n"
                                   "    # Runs.\n"
                                   "    chase\n"
                                   "    patrol\n");
    ct::wrapBehaviourNode(tree, 1, BehaviourNodeKind::TimeLimit);
    CHECK(text(tree) == "# The whole tree.\n"
                        "fallback\n"
                        "    time_limit 5\n"
                        "        # Runs.\n"
                        "        chase\n"
                        "    patrol\n");
    CHECK(tree.nodes[1].memory == 0);

    // The root's note stays at the top.
    ct::wrapBehaviourNode(tree, 0, BehaviourNodeKind::Sequence);
    CHECK(tree.nodes[0].note == "The whole tree.");
    CHECK(tree.nodes[1].note.empty());
    CHECK(ct::canUnwrapBehaviourNode(tree, 0));
    ct::unwrapBehaviourNode(tree, 0);
    CHECK(tree.nodes[0].kind == BehaviourNodeKind::Fallback);
    CHECK(tree.nodes[0].note == "The whole tree.");

    // A decorator can't take a composite's two children.
    CHECK_FALSE(ct::canUnwrapBehaviourNode(tree, 0));
    ct::wrapBehaviourNode(tree, 0, BehaviourNodeKind::Invert);
    CHECK_FALSE(ct::canUnwrapBehaviourNode(tree, 1));
    ct::unwrapBehaviourNode(tree, 0);

    CHECK(ct::canUnwrapBehaviourNode(tree, 1));
    ct::unwrapBehaviourNode(tree, 1);
    CHECK(text(tree) == "# The whole tree.\n"
                        "fallback\n"
                        "    # Runs.\n"
                        "    chase\n"
                        "    patrol\n");

    CHECK(ct::canChangeBehaviourKind(tree, 0, BehaviourNodeKind::Cooldown));
    CHECK_FALSE(ct::canChangeBehaviourKind(tree, 0, BehaviourNodeKind::Wait));
    CHECK(ct::canChangeBehaviourKind(tree, 2, BehaviourNodeKind::Wait));
    ct::changeBehaviourKind(tree, 2, BehaviourNodeKind::Wait);
    CHECK(tree.nodes[2].value == 1.0f);
    CHECK(tree.nodes[2].memory == 0);
    ct::changeBehaviourKind(tree, 0, BehaviourNodeKind::Sequence);
    CHECK(text(tree) == "# The whole tree.\n"
                        "sequence\n"
                        "    # Runs.\n"
                        "    chase\n"
                        "    wait 1\n");
}

TEST_CASE("a behaviour tree is laid out left to right, as its file reads") {
    const ct::BehaviourTree tree = draft(kTree);
    std::vector<ct::BehaviourBox> sizes(tree.nodes.size(), {0.0f, 0.0f, 100.0f, 40.0f});
    sizes[2].width = 150.0f; // the fallback is wider than the cooldown in its column
    sizes[6].height = 100.0f;
    const std::vector<ct::BehaviourBox> boxes = ct::layoutBehaviourTree(tree, sizes, 50.0f, 10.0f);
    REQUIRE(boxes.size() == tree.nodes.size());
    // Columns.
    CHECK(boxes[0].x == 0.0f);
    CHECK(boxes[1].x == 150.0f);
    CHECK(boxes[5].x == 150.0f);
    CHECK(boxes[3].x == 350.0f);
    CHECK(boxes[6].x == 350.0f);
    // Rows: see (40), the fallback's two (40 + 10 + 40), the patrol (100), with gaps of 10.
    CHECK(boxes[1].y == 0.0f);
    CHECK(boxes[3].y == 50.0f);
    CHECK(boxes[4].y == 100.0f);
    CHECK(boxes[2].y == 95.0f - 20.0f);
    CHECK(boxes[6].y == 150.0f);
    CHECK(boxes[5].y == 200.0f - 20.0f);
    CHECK(boxes[0].y == 125.0f - 20.0f);
    CHECK(boxes[6].height == 100.0f);

    // Dropping the patrol's cooldown between see and the fallback.
    CHECK(ct::behaviourDropPosition(tree, boxes, 0, 5, 30.0f) == 1);
    CHECK(ct::behaviourDropPosition(tree, boxes, 0, 5, -5.0f) == 0);
    CHECK(ct::behaviourDropPosition(tree, boxes, 0, 5, 500.0f) == 2);
    // A node from elsewhere joining the fallback's children, between them.
    CHECK(ct::behaviourDropPosition(tree, boxes, 2, 1, 90.0f) == 1);
}
