#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ct {

class ContentPaths;
struct GameSettings;

// What an enemy decides to do each tick, as a tree of nodes (docs/Behaviours.md). Each enemy type
// names one, behaviours/<name>.behaviour through the content layers. The tree is read again from
// the root every tick: composites pick which children run, decorators change what one child
// does, conditions ask a question, and actions do something, each saying whether it succeeded,
// failed or is still running. An action carries on from one tick to the next through the enemy's
// own state (sim/Enemy.h), so a tree saves, rewinds and replicates with the rest of the game.

enum class BehaviourStatus : std::uint8_t { Success, Failure, Running };

enum class BehaviourCategory : std::uint8_t { Composite, Decorator, Condition, Action };

// The order is the save format of nothing (trees are files of names), so it can change freely.
enum class BehaviourNodeKind : std::uint8_t {
    // Composites.
    Sequence, // its children in turn, while each succeeds
    Fallback, // its children in turn, until one doesn't fail
    Parallel, // all its children every tick; it does what its first one does
    // Decorators.
    Invert,    // success is failure and failure success
    Succeed,   // failure is success
    Cooldown,  // after its child succeeds, fails for a while without running it
    TimeLimit, // fails once its child has run for a while
    // Conditions.
    HasTarget,
    TargetVisible,
    TargetInReach,
    TargetInRange,
    TargetWithin,
    HealthBelow,
    Chance,
    // Actions.
    See,
    Hear,
    React,
    TakeCover,
    Strike,
    Chase,
    KeepDistance,
    Strafe,
    FireBurst,
    Search,
    Patrol,
    Wait,
};

// A node's one setting, if it has one: "cooldown 2" is two seconds.
struct BehaviourParameter {
    std::string_view name; // empty for none
    std::string_view unit;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float defaultValue = 0.0f;
};

struct BehaviourNodeInfo {
    BehaviourNodeKind kind;
    std::string_view name; // as files write it
    BehaviourCategory category;
    BehaviourParameter parameter;
    // Whether it keeps a value from tick to tick, in one of the enemy's memory slots.
    bool remembers = false;
    std::string_view description;
};

// Every kind of node.
std::span<const BehaviourNodeInfo> behaviourNodes();
const BehaviourNodeInfo& behaviourNodeInfo(BehaviourNodeKind kind);
const BehaviourNodeInfo* findBehaviourNode(std::string_view name);

// Limits on a tree, so an enemy's running state has a fixed size (EnemyBehaviour, sim/Enemy.h).
inline constexpr std::uint32_t kMaxBehaviourNodes = 128;
inline constexpr std::uint32_t kMaxBehaviourDepth = 32;
inline constexpr std::uint32_t kMaxBehaviourMemory = 16;
inline constexpr std::size_t kMaxBehaviourFileSize = 64 * 1024;
inline constexpr std::uint8_t kNoBehaviourMemory = 0xFF;

struct BehaviourNode {
    BehaviourNodeKind kind = BehaviourNodeKind::Sequence;
    std::uint8_t memory = kNoBehaviourMemory; // its slot, for nodes that remember
    std::uint16_t end = 0;                    // the index just past its last descendant
    float value = 0.0f;                       // its parameter
    // The comments on the lines above it and at the end of its own line, one a line.
    std::string note;

    friend bool operator==(const BehaviourNode&, const BehaviourNode&) = default;
};

// The nodes in depth-first order: the root first, and each node's children after it, the first
// at the next index and each other one at its older sibling's end.
struct BehaviourTree {
    std::string name;
    std::vector<BehaviourNode> nodes;

    friend bool operator==(const BehaviourTree&, const BehaviourTree&) = default;
};

// Reads a tree file: one node a line, children indented further than their parent with spaces,
// a node's setting after its name, and '#' starting a comment, which belongs to the node below it
// (or on its line); comments after the last node are dropped. Rejects anything malformed or past
// the limits with an error naming the line; a tree that loads is safe to run.
std::optional<BehaviourTree> parseBehaviourTree(std::string_view text, std::string_view name,
                                                std::string& error);
// The file text of a tree; parsing it gives the same tree.
std::string formatBehaviourTree(const BehaviourTree& tree);

// The tree enemies use when their type names none, or one that won't load: the charger's and the
// gunner's. Its text is content/behaviours/standard.behaviour.
std::string_view standardBehaviourText();
const BehaviourTree& standardBehaviourTree();

// Reads the tree each enemy type names into the settings (GameSettings::behaviours, and each
// type's behaviourIndex). Types whose tree is missing or won't load use the standard one; the
// problems are returned.
std::vector<std::string> loadBehaviourTrees(const ContentPaths& content, GameSettings& settings);
// The tree an enemy type runs.
const BehaviourTree& behaviourOf(const GameSettings& settings, std::uint32_t enemyType);

} // namespace ct
