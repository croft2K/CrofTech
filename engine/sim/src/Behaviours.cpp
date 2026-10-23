#include "EnemyTick.h"
#include "LevelLogic.h"

#include "croftech/core/Assert.h"
#include "croftech/core/Random.h"
#include "croftech/sim/Health.h"

#include <algorithm>
#include <cstdint>

namespace ct {
namespace {

TickIndex ticksOf(float seconds) {
    return static_cast<TickIndex>(std::max(seconds, 0.0f) * static_cast<float>(kTickRate) + 0.5f);
}

} // namespace

BehaviourStatus LevelLogic::runNode(EnemyTick& think, std::uint32_t index) {
    using enum BehaviourNodeKind;
    using enum BehaviourStatus;
    const BehaviourTree& tree = think.tree;
    CT_ASSERT(index < tree.nodes.size() && index < kMaxBehaviourNodes);
    const BehaviourNode& node = tree.nodes[index];
    const std::uint32_t word = index / 32;
    const std::uint32_t bit = 1u << (index % 32);
    // Reached last tick too: it carries on rather than starting afresh.
    const bool carryOn = (think.behaviour.reached[word] & bit) != 0;
    think.reached[word] |= bit;
    std::uint32_t scratch = 0;
    std::uint32_t& memory =
        node.memory < kMaxBehaviourMemory ? think.behaviour.memory[node.memory] : scratch;
    const TickIndex tick = m_state.tick;
    const std::uint32_t firstChild = index + 1;

    BehaviourStatus status = Failure;
    switch (node.kind) {
    case Sequence:
    case Fallback: {
        // A sequence goes on while its children succeed, a fallback while they fail.
        const BehaviourStatus goOn = node.kind == Sequence ? Success : Failure;
        status = goOn;
        for (std::uint32_t child = firstChild; child < node.end; child = tree.nodes[child].end) {
            status = runNode(think, child);
            if (status != goOn) {
                break;
            }
        }
        break;
    }
    case Parallel:
        for (std::uint32_t child = firstChild; child < node.end; child = tree.nodes[child].end) {
            const BehaviourStatus each = runNode(think, child);
            if (child == firstChild) {
                status = each;
            }
        }
        break;
    case Invert:
        status = runNode(think, firstChild);
        status = status == Success ? Failure : status == Failure ? Success : Running;
        break;
    case Succeed:
        status = runNode(think, firstChild) == Running ? Running : Success;
        break;
    case Cooldown: {
        // The memory is the tick it may run again; one further off than the cooldown is from
        // another tree, and is ignored.
        const TickIndex length = ticksOf(node.value);
        if (memory > tick && memory - tick <= length) {
            break;
        }
        status = runNode(think, firstChild);
        if (status == Success) {
            memory = tick + length;
        }
        break;
    }
    case TimeLimit:
        // A time further off than the limit is from another tree.
        if (!carryOn || memory > tick + ticksOf(node.value)) {
            memory = tick + ticksOf(node.value);
        }
        if (tick < memory) {
            status = runNode(think, firstChild);
        }
        break;
    case Chance:
        if (!carryOn) {
            memory = randomUnit(think.enemy.random) < node.value ? 1u : 0u;
        }
        status = memory != 0 ? Success : Failure;
        break;
    case HealthBelow:
        status = static_cast<float>(think.health.health) <
                         node.value * static_cast<float>(think.type.health)
                     ? Success
                     : Failure;
        break;
    case Wait:
        if (!carryOn || memory > tick + ticksOf(node.value)) {
            memory = tick + ticksOf(node.value);
        }
        status = tick >= memory ? Success : Running;
        break;
    default:
        status = runEnemyNode(think, node);
        break;
    }
    if (status != Failure) {
        think.active[word] |= bit;
    }
    return status;
}

} // namespace ct
