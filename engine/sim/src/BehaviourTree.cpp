#include "croftech/sim/BehaviourTree.h"

#include "croftech/core/Assert.h"
#include "croftech/core/ConfigFile.h"
#include "croftech/core/Content.h"
#include "croftech/sim/GameSettings.h"

#include <algorithm>
#include <format>
#include <iterator>

namespace ct {
namespace {

using enum BehaviourNodeKind;
using enum BehaviourCategory;

constexpr BehaviourParameter kNoParameter{};
constexpr BehaviourParameter kSeconds{"seconds", "s", 0.0f, 600.0f, 1.0f};
constexpr BehaviourParameter kShare{"share", "", 0.0f, 1.0f, 0.5f};

// Listed in the palette's order: composites, decorators, conditions, then actions.
constexpr BehaviourNodeInfo kNodes[] = {
    {Sequence, "sequence", Composite, kNoParameter, false,
     "Runs its children in turn while each succeeds: fails when one fails, is running while one "
     "is, and succeeds when all have. Read again every tick, so a condition first stops the rest "
     "as soon as it stops holding"},
    {Fallback, "fallback", Composite, kNoParameter, false,
     "Tries its children in turn until one doesn't fail, and does what that one does; fails if "
     "they all do. The first that can act wins, so put the most urgent first"},
    {Parallel, "parallel", Composite, kNoParameter, false,
     "Runs all its children every tick, and does what its first one does: the others act "
     "beside it, such as shooting while chasing"},

    {Invert, "invert", Decorator, kNoParameter, false,
     "Its child's success is failure and failure success"},
    {Succeed, "succeed", Decorator, kNoParameter, false,
     "Succeeds when its child fails, so a sequence carries on past it"},
    {Cooldown,
     "cooldown",
     Decorator,
     {"seconds", "s", 0.0f, 600.0f, 2.0f},
     true,
     "After its child succeeds, fails for this long without running it"},
    {TimeLimit,
     "time_limit",
     Decorator,
     {"seconds", "s", 0.0f, 600.0f, 5.0f},
     true,
     "Fails once its child has been running this long without a break"},

    {HasTarget, "has_target", Condition, kNoParameter, false,
     "Whether it is after a living player"},
    {TargetVisible, "target_visible", Condition, kNoParameter, false,
     "Whether it can see the player it is after"},
    {TargetInReach, "target_in_reach", Condition, kNoParameter, false,
     "Whether the player it is after is in reach of a swing (melee_range), and in sight"},
    {TargetInRange, "target_in_range", Condition, kNoParameter, false,
     "Whether it has a weapon and the player it is after is in sight within its attack_range"},
    {TargetWithin,
     "target_within",
     Condition,
     {"metres", "m", 0.0f, 1000.0f, 5.0f},
     false,
     "Whether the player it is after is within this distance"},
    {HealthBelow, "health_below", Condition, kShare, false,
     "Whether its health is below this share of its type's"},
    {Chance, "chance", Condition, kShare, true,
     "Succeeds with this chance, decided when it is reached and kept while it keeps being "
     "reached every tick"},

    {See, "see", Action, kNoParameter, false,
     "While it is calm or searching, notices the nearest player it can see in its view cone. "
     "Always succeeds"},
    {Hear, "hear", Action, kNoParameter, false,
     "While it is calm, searching or has lost its target, notices the nearest player it hears "
     "shoot or run, unless it lies in ambush. Always succeeds"},
    {React, "react", Action, kNoParameter, false,
     "Having just noticed a player, stands and turns to them for its reaction_time; fails "
     "otherwise"},
    {TakeCover, "take_cover", Action, kNoParameter, false,
     "When it has just been hurt in a fight or ended a burst, may (by its cover_chance) run to a "
     "spot the target can't see, and stay there for its cover_time; fails when it isn't hiding"},
    {Strike, "strike", Action, kNoParameter, false,
     "With the target in reach, faces it and swings (melee_damage, after melee_windup); "
     "succeeds as a swing lands, fails out of reach"},
    {Chase, "chase", Action, kNoParameter, false,
     "Goes after the target along the navigation mesh, or to where it last saw it; fails once "
     "it has got there, or long after, without finding it"},
    {KeepDistance, "keep_distance", Action, kNoParameter, false,
     "While chasing with the target in range, stops closing in at its keep_distance; fails "
     "otherwise"},
    {Strafe, "strafe", Action, kNoParameter, false,
     "While chasing with the target in range, sidesteps one way then the other (strafe_time); "
     "fails otherwise"},
    {FireBurst, "fire_burst", Action, kNoParameter, false,
     "While chasing with the target in range, shoots at it in bursts (burst, burst_pause, "
     "aim_error); fails otherwise"},
    {Search, "search", Action, kNoParameter, false,
     "Having lost its target, looks about where it was last seen for its search_time; fails "
     "otherwise"},
    {Patrol, "patrol", Action, kNoParameter, false,
     "Forgets its target and walks its patrol, or back to its post, and stands there; always "
     "running"},
    {Wait, "wait", Action, kSeconds, true, "Stands still for this long, then succeeds"},
};

constexpr std::string_view kStandardText =
    R"(# The standard enemy, which the charger and gunner both are: what each does comes from its
# settings, as a swing needs melee_damage, shooting a weapon, and running for cover a cover_chance.
sequence
    # Calm, it watches and listens for players.
    see
    hear
    fallback
        sequence
            has_target
            fallback
                react
                take_cover
                strike
                # Fights at a distance with a weapon.
                parallel
                    chase
                    keep_distance
                    strafe
                    fire_burst
                search
        patrol
)";

constexpr std::string_view kStandardName = "standard";

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

void addNote(std::string& note, std::string_view line) {
    if (!note.empty()) {
        note += '\n';
    }
    note += line;
}

// What follows '#', without the space usually after it.
std::string_view commentText(std::string_view comment) {
    comment.remove_prefix(1);
    return trim(comment);
}

} // namespace

std::span<const BehaviourNodeInfo> behaviourNodes() { return kNodes; }

const BehaviourNodeInfo& behaviourNodeInfo(BehaviourNodeKind kind) {
    for (const BehaviourNodeInfo& info : kNodes) {
        if (info.kind == kind) {
            return info;
        }
    }
    CT_ASSERT(false);
    return kNodes[0];
}

const BehaviourNodeInfo* findBehaviourNode(std::string_view name) {
    for (const BehaviourNodeInfo& info : kNodes) {
        if (info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

std::optional<BehaviourTree> parseBehaviourTree(std::string_view text, std::string_view name,
                                                std::string& error) {
    if (text.size() > kMaxBehaviourFileSize) {
        error = std::format("is over {} KB", kMaxBehaviourFileSize / 1024);
        return std::nullopt;
    }
    struct Open {
        std::size_t indent = 0;
        std::uint32_t node = 0;
        std::size_t childIndent = 0; // of its children, once it has one
        std::uint32_t children = 0;
    };
    BehaviourTree tree;
    tree.name = std::string(name);
    std::vector<Open> open;
    std::string note;
    std::uint32_t memory = 0;
    const auto close = [&] {
        tree.nodes[open.back().node].end = static_cast<std::uint16_t>(tree.nodes.size());
        open.pop_back();
    };

    std::size_t lineNumber = 0;
    while (!text.empty()) {
        ++lineNumber;
        const std::size_t newline = text.find('\n');
        std::string_view line = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::size_t indent = line.find_first_not_of(' ');
        if (indent == std::string_view::npos) {
            continue;
        }
        line.remove_prefix(indent);
        if (line.front() == '\t') {
            error = std::format("line {}: indent with spaces, not tabs", lineNumber);
            return std::nullopt;
        }
        if (line.front() == '#') {
            addNote(note, commentText(line));
            continue;
        }
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            addNote(note, commentText(line.substr(hash)));
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        const std::size_t space = line.find_first_of(" \t");
        const std::string_view word = line.substr(0, space);
        const std::string_view rest =
            space == std::string_view::npos ? std::string_view{} : trim(line.substr(space));
        const BehaviourNodeInfo* info = findBehaviourNode(word);
        if (info == nullptr) {
            error = std::format("line {}: there is no node '{}'", lineNumber, word);
            return std::nullopt;
        }
        BehaviourNode node;
        node.kind = info->kind;
        node.value = info->parameter.defaultValue;
        if (!rest.empty()) {
            const BehaviourParameter& parameter = info->parameter;
            if (parameter.name.empty()) {
                error = std::format("line {}: {} takes no setting", lineNumber, word);
                return std::nullopt;
            }
            if (!parseFloat(rest, node.value) || node.value < parameter.minimum ||
                node.value > parameter.maximum) {
                error = std::format("line {}: {} takes {} from {} to {}", lineNumber, word,
                                    parameter.name, parameter.minimum, parameter.maximum);
                return std::nullopt;
            }
        }

        if (tree.nodes.empty()) {
            if (indent != 0) {
                error = std::format("line {}: the first node isn't indented", lineNumber);
                return std::nullopt;
            }
        } else {
            while (!open.empty() && open.back().indent >= indent) {
                close();
            }
            if (open.empty()) {
                error = std::format("line {}: a tree has one node at the top, so this must be "
                                    "indented under it",
                                    lineNumber);
                return std::nullopt;
            }
            Open& parent = open.back();
            const BehaviourNodeInfo& parentInfo = behaviourNodeInfo(tree.nodes[parent.node].kind);
            if (parentInfo.category == Condition || parentInfo.category == Action) {
                error =
                    std::format("line {}: {} takes nothing under it", lineNumber, parentInfo.name);
                return std::nullopt;
            }
            if (parentInfo.category == Decorator && parent.children == 1) {
                error =
                    std::format("line {}: {} takes one node under it", lineNumber, parentInfo.name);
                return std::nullopt;
            }
            if (parent.children > 0 && indent != parent.childIndent) {
                error =
                    std::format("line {}: indented differently from the node above it", lineNumber);
                return std::nullopt;
            }
            parent.childIndent = indent;
            ++parent.children;
        }
        if (tree.nodes.size() == kMaxBehaviourNodes) {
            error =
                std::format("line {}: a tree has at most {} nodes", lineNumber, kMaxBehaviourNodes);
            return std::nullopt;
        }
        if (open.size() == kMaxBehaviourDepth) {
            error = std::format("line {}: a tree is at most {} nodes deep", lineNumber,
                                kMaxBehaviourDepth);
            return std::nullopt;
        }
        if (info->remembers) {
            if (memory == kMaxBehaviourMemory) {
                error = std::format("line {}: a tree has at most {} nodes that remember (such as "
                                    "{})",
                                    lineNumber, kMaxBehaviourMemory, word);
                return std::nullopt;
            }
            node.memory = static_cast<std::uint8_t>(memory++);
        }
        node.note = std::move(note);
        note.clear();
        open.push_back({indent, static_cast<std::uint32_t>(tree.nodes.size())});
        tree.nodes.push_back(std::move(node));
    }
    if (tree.nodes.empty()) {
        error = "has no nodes";
        return std::nullopt;
    }
    while (!open.empty()) {
        close();
    }
    // Composites need children to choose from, and decorators one to change.
    for (std::uint32_t i = 0; i < tree.nodes.size(); ++i) {
        const BehaviourNodeInfo& info = behaviourNodeInfo(tree.nodes[i].kind);
        if ((info.category == Composite || info.category == Decorator) &&
            tree.nodes[i].end == i + 1) {
            error = std::format("its {} (node {}) has nothing under it", info.name, i + 1);
            return std::nullopt;
        }
    }
    return tree;
}

std::string formatBehaviourTree(const BehaviourTree& tree) {
    std::string text;
    std::vector<std::uint32_t> ends; // of the nodes this one is under
    for (std::uint32_t i = 0; i < tree.nodes.size(); ++i) {
        while (!ends.empty() && ends.back() <= i) {
            ends.pop_back();
        }
        const BehaviourNode& node = tree.nodes[i];
        const std::string indent(ends.size() * 4, ' ');
        std::string_view note = node.note;
        while (!note.empty()) {
            const std::size_t newline = note.find('\n');
            const std::string_view line = note.substr(0, newline);
            text +=
                line.empty() ? std::format("{}#\n", indent) : std::format("{}# {}\n", indent, line);
            note.remove_prefix(newline == std::string_view::npos ? note.size() : newline + 1);
        }
        const BehaviourNodeInfo& info = behaviourNodeInfo(node.kind);
        text += indent;
        text += info.name;
        if (!info.parameter.name.empty()) {
            text += ' ';
            text += formatFloat(node.value);
        }
        text += '\n';
        ends.push_back(node.end);
    }
    return text;
}

std::string_view standardBehaviourText() { return kStandardText; }

const BehaviourTree& standardBehaviourTree() {
    static const BehaviourTree tree = [] {
        std::string error;
        std::optional<BehaviourTree> parsed =
            parseBehaviourTree(kStandardText, kStandardName, error);
        CT_ASSERT(parsed.has_value());
        return std::move(*parsed);
    }();
    return tree;
}

std::vector<std::string> loadBehaviourTrees(const ContentPaths& content, GameSettings& settings) {
    std::vector<std::string> problems;
    settings.behaviours.clear();
    for (EnemySettings& enemy : settings.enemies) {
        enemy.behaviourIndex = kNoBehaviour;
        const std::string_view name =
            enemy.behaviour.empty() ? kStandardName : std::string_view(enemy.behaviour);
        const auto loaded =
            std::find_if(settings.behaviours.begin(), settings.behaviours.end(),
                         [&](const BehaviourTree& tree) { return tree.name == name; });
        if (loaded != settings.behaviours.end()) {
            enemy.behaviourIndex = static_cast<std::uint32_t>(loaded - settings.behaviours.begin());
            continue;
        }
        if (!isContentName(name)) {
            problems.push_back(
                std::format("{}: '{}' can't be a behaviour's name", enemy.name, name));
            continue;
        }
        const std::string path = std::format("behaviours/{}.behaviour", name);
        const std::optional<std::filesystem::path> found = content.find(path);
        if (!found) {
            if (name != kStandardName) {
                problems.push_back(std::format("{}: there is no {}", enemy.name, path));
            }
            continue;
        }
        const std::optional<std::string> text = readTextFile(*found, kMaxBehaviourFileSize);
        if (!text) {
            problems.push_back(std::format("{}: couldn't read {}", enemy.name, path));
            continue;
        }
        std::string error;
        std::optional<BehaviourTree> tree = parseBehaviourTree(*text, name, error);
        if (!tree) {
            problems.push_back(std::format("{}: {}", path, error));
            continue;
        }
        enemy.behaviourIndex = static_cast<std::uint32_t>(settings.behaviours.size());
        settings.behaviours.push_back(std::move(*tree));
    }
    return problems;
}

const BehaviourTree& behaviourOf(const GameSettings& settings, std::uint32_t enemyType) {
    if (enemyType < settings.enemies.size() &&
        settings.enemies[enemyType].behaviourIndex < settings.behaviours.size()) {
        return settings.behaviours[settings.enemies[enemyType].behaviourIndex];
    }
    return standardBehaviourTree();
}

} // namespace ct
