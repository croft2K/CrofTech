// Fuzzes reading behaviour trees, which may come with a game or level downloaded from a server. A
// tree that loads keeps to the limits an enemy's running state is sized for, its node ranges nest
// properly, and written out it reads back the same. Read as a draft (as the editor does), the
// same holds but for the limits and child counts, and a draft loads whole exactly when it has
// no problems.

#include "croftech/sim/BehaviourTree.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace {

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

// Node ranges nest, leaves have nothing under them, and memory slots go in node order.
void checkShape(const ct::BehaviourTree& tree) {
    const auto& nodes = tree.nodes;
    check(!nodes.empty() && nodes[0].end == nodes.size());
    std::uint32_t memory = 0;
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        const ct::BehaviourNode& node = nodes[i];
        const ct::BehaviourNodeInfo& info = ct::behaviourNodeInfo(node.kind);
        check(node.end > i && node.end <= nodes.size());
        for (std::uint32_t child = i + 1; child < node.end; child = nodes[child].end) {
            check(nodes[child].end <= node.end);
        }
        if (info.category == ct::BehaviourCategory::Condition ||
            info.category == ct::BehaviourCategory::Action) {
            check(node.end == i + 1);
        }
        if (info.remembers) {
            check(node.memory == (memory < ct::kMaxBehaviourMemory ? memory : 0xFF));
            ++memory;
        } else {
            check(node.memory == ct::kNoBehaviourMemory);
        }
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::string error;
    const std::optional<ct::BehaviourTree> draft =
        ct::parseBehaviourTree(text, "fuzz", error, ct::BehaviourReading::Draft);
    const std::optional<ct::BehaviourTree> tree = ct::parseBehaviourTree(text, "fuzz", error);
    if (!draft) {
        check(!error.empty() && !tree);
        return 0;
    }
    checkShape(*draft);
    const std::optional<ct::BehaviourTree> draftAgain = ct::parseBehaviourTree(
        ct::formatBehaviourTree(*draft), "fuzz", error, ct::BehaviourReading::Draft);
    check(draftAgain.has_value() && *draftAgain == *draft);
    check(ct::behaviourProblems(*draft).empty() == tree.has_value());
    if (!tree) {
        return 0;
    }
    check(*tree == *draft);
    const auto& nodes = tree->nodes;
    check(nodes.size() <= ct::kMaxBehaviourNodes);
    std::uint32_t memory = 0;
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        const ct::BehaviourNode& node = nodes[i];
        const ct::BehaviourNodeInfo& info = ct::behaviourNodeInfo(node.kind);
        std::uint32_t children = 0;
        for (std::uint32_t child = i + 1; child < node.end; child = nodes[child].end) {
            ++children;
        }
        if (info.category == ct::BehaviourCategory::Composite) {
            check(children >= 1);
        } else if (info.category == ct::BehaviourCategory::Decorator) {
            check(children == 1);
        }
        memory += info.remembers ? 1 : 0;
    }
    check(memory <= ct::kMaxBehaviourMemory);
    const std::optional<ct::BehaviourTree> again =
        ct::parseBehaviourTree(ct::formatBehaviourTree(*tree), "fuzz", error);
    check(again.has_value() && *again == *tree);
    return 0;
}
