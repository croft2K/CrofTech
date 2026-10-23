// Fuzzes reading behaviour trees, which may come with a game or level downloaded from a server. A
// tree that loads keeps to the limits an enemy's running state is sized for, its node ranges nest
// properly, and written out it reads back the same.

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

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view text(reinterpret_cast<const char*>(data), size);
    std::string error;
    const std::optional<ct::BehaviourTree> tree = ct::parseBehaviourTree(text, "fuzz", error);
    if (!tree) {
        check(!error.empty());
        return 0;
    }
    const auto& nodes = tree->nodes;
    check(!nodes.empty() && nodes.size() <= ct::kMaxBehaviourNodes);
    check(nodes[0].end == nodes.size());
    std::uint32_t memory = 0;
    for (std::uint32_t i = 0; i < nodes.size(); ++i) {
        const ct::BehaviourNode& node = nodes[i];
        const ct::BehaviourNodeInfo& info = ct::behaviourNodeInfo(node.kind);
        check(node.end > i && node.end <= nodes.size());
        std::uint32_t children = 0;
        for (std::uint32_t child = i + 1; child < node.end; child = nodes[child].end) {
            check(nodes[child].end <= node.end);
            ++children;
        }
        switch (info.category) {
        case ct::BehaviourCategory::Composite:
            check(children >= 1);
            break;
        case ct::BehaviourCategory::Decorator:
            check(children == 1);
            break;
        default:
            check(children == 0);
            break;
        }
        if (info.remembers) {
            check(node.memory == memory++);
        } else {
            check(node.memory == ct::kNoBehaviourMemory);
        }
    }
    check(memory <= ct::kMaxBehaviourMemory);
    const std::optional<ct::BehaviourTree> again =
        ct::parseBehaviourTree(ct::formatBehaviourTree(*tree), "fuzz", error);
    check(again.has_value() && *again == *tree);
    return 0;
}
