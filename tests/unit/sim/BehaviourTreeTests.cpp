#include "TestFiles.h"

#include "croftech/core/ConfigFile.h"
#include "croftech/core/Content.h"
#include "croftech/script/ScriptProgram.h"
#include "croftech/sim/BehaviourTree.h"
#include "croftech/sim/GameSettings.h"

#include <doctest/doctest.h>

#include <optional>
#include <ostream> // doctest prints string_view through operator<<
#include <string>
#include <string_view>
#include <vector>

namespace {

using ct::BehaviourNodeKind;

std::optional<ct::BehaviourTree> parse(std::string_view text, std::string* error = nullptr) {
    std::string problem;
    std::optional<ct::BehaviourTree> tree = ct::parseBehaviourTree(text, "test", problem);
    if (error != nullptr) {
        *error = problem;
    }
    return tree;
}

std::string problemWith(std::string_view text) {
    std::string error;
    CHECK_FALSE(parse(text, &error).has_value());
    return error;
}

} // namespace

TEST_CASE("a behaviour tree is read with its children in depth-first order") {
    const auto tree = parse("# Top\n"
                            "fallback\n"
                            "    sequence  # first\n"
                            "        has_target\n"
                            "        cooldown 2.5\n"
                            "            chase\n"
                            "\n"
                            "    # last\n"
                            "    wait\n");
    REQUIRE(tree.has_value());
    REQUIRE(tree->nodes.size() == 6);
    CHECK(tree->name == "test");
    const std::vector<ct::BehaviourNode>& nodes = tree->nodes;
    CHECK(nodes[0].kind == BehaviourNodeKind::Fallback);
    CHECK(nodes[0].end == 6);
    CHECK(nodes[0].note == "Top");
    CHECK(nodes[1].kind == BehaviourNodeKind::Sequence);
    CHECK(nodes[1].end == 5);
    CHECK(nodes[1].note == "first");
    CHECK(nodes[2].end == 3);
    CHECK(nodes[3].kind == BehaviourNodeKind::Cooldown);
    CHECK(nodes[3].value == 2.5f);
    CHECK(nodes[3].memory == 0);
    CHECK(nodes[3].end == 5);
    CHECK(nodes[4].kind == BehaviourNodeKind::Chase);
    CHECK(nodes[5].kind == BehaviourNodeKind::Wait);
    // Left out, a setting is the node's default; its memory slot is the next.
    CHECK(nodes[5].value == 1.0f);
    CHECK(nodes[5].memory == 1);
    CHECK(nodes[5].note == "last");
    CHECK(nodes[2].memory == ct::kNoBehaviourMemory);
}

TEST_CASE("a behaviour tree written out reads back the same") {
    const auto tree = parse("sequence\n"
                            "  # two\n"
                            "  # lines\n"
                            "  chance 0.25\n"
                            "  invert\n"
                            "    target_within 12\n");
    REQUIRE(tree.has_value());
    const std::string text = ct::formatBehaviourTree(*tree);
    CHECK(text == "sequence\n"
                  "    # two\n"
                  "    # lines\n"
                  "    chance 0.25\n"
                  "    invert\n"
                  "        target_within 12\n");
    CHECK(parse(text) == tree);
    CHECK(ct::formatBehaviourTree(ct::standardBehaviourTree()) == ct::standardBehaviourText());

    // A script node's setting is a name.
    const auto scripted = parse("fallback\n    script keep-back_2\n    patrol\n");
    REQUIRE(scripted.has_value());
    CHECK(scripted->nodes[1].kind == BehaviourNodeKind::Script);
    CHECK(scripted->nodes[1].script == "keep-back_2");
    CHECK(ct::formatBehaviourTree(*scripted) == "fallback\n    script keep-back_2\n    patrol\n");

    // Blank comment lines part a note's paragraphs but don't end it.
    const auto spaced = parse("sequence\n"
                              "    #\n"
                              "    # one\n"
                              "    #\n"
                              "    # two\n"
                              "    #\n"
                              "    chance 0.5 #\n");
    REQUIRE(spaced.has_value());
    CHECK(spaced->nodes[1].note == "one\n\ntwo");
    CHECK(parse(ct::formatBehaviourTree(*spaced)) == spaced);
}

TEST_CASE("malformed behaviour trees are refused, naming the line") {
    CHECK(problemWith("") == "has no nodes");
    CHECK(problemWith("# only a comment\n") == "has no nodes");
    CHECK(problemWith("sequence\n    chace\n") == "line 2: there is no node 'chace'");
    CHECK(problemWith("  chase\n") == "line 1: the first node isn't indented");
    CHECK(problemWith("chase\nsee\n").starts_with("line 2: a tree has one node at the top"));
    CHECK(problemWith("chase\n    see\n") == "line 2: chase takes nothing under it");
    CHECK(problemWith("invert\n    see\n    hear\n") == "line 3: invert takes one node under it");
    CHECK(problemWith("sequence\n    see\n  hear\n") ==
          "line 3: indented differently from the node above it");
    CHECK(problemWith("sequence\n\tsee\n") == "line 2: indent with spaces, not tabs");
    CHECK(problemWith("sequence\n    see 2\n") == "line 2: see takes no setting");
    CHECK(problemWith("sequence\n    wait forever\n").starts_with("line 2: wait takes seconds"));
    CHECK(problemWith("sequence\n    chance 2\n").starts_with("line 2: chance takes share"));
    CHECK(problemWith("sequence\n    see\n    fallback\n") ==
          "its fallback (node 3) has nothing under it");
    CHECK(
        problemWith("sequence\n    script\n").starts_with("line 2: script takes a script's name"));
    CHECK(problemWith("sequence\n    script ../x\n")
              .starts_with("line 2: script takes a script's name"));

    // The limits that keep an enemy's running state a fixed size.
    std::string wide = "sequence\n";
    for (std::uint32_t i = 0; i < ct::kMaxBehaviourNodes; ++i) {
        wide += "    see\n";
    }
    CHECK(problemWith(wide).ends_with("a tree has at most 128 nodes"));
    std::string deep;
    for (std::uint32_t i = 0; i <= ct::kMaxBehaviourDepth; ++i) {
        deep += std::string(i * 2, ' ') + "invert\n";
    }
    CHECK(problemWith(deep).ends_with("a tree is at most 32 nodes deep"));
    std::string waits = "sequence\n";
    for (std::uint32_t i = 0; i <= ct::kMaxBehaviourMemory; ++i) {
        waits += "    wait\n";
    }
    CHECK(problemWith(waits).find("at most 16 nodes that remember") != std::string::npos);
    CHECK(problemWith(std::string(ct::kMaxBehaviourFileSize + 1, ' ')) == "is over 64 KB");
}

TEST_CASE("a draft tree opens with its problems on its nodes") {
    const std::string_view text = "sequence\n"
                                  "    fallback\n"
                                  "    invert\n"
                                  "        see\n"
                                  "        hear\n"
                                  "    cooldown\n";
    CHECK(problemWith(text) == "line 5: invert takes one node under it");
    std::string error;
    const auto draft = ct::parseBehaviourTree(text, "test", error, ct::BehaviourReading::Draft);
    REQUIRE(draft.has_value());
    REQUIRE(draft->nodes.size() == 6);
    CHECK(draft->nodes[2].end == 5);
    CHECK(draft->nodes[5].memory == 0);
    const std::vector<ct::BehaviourProblem> problems = ct::behaviourProblems(*draft);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0] == ct::BehaviourProblem{1, "has nothing under it"});
    CHECK(problems[1] == ct::BehaviourProblem{2, "takes one node under it"});
    CHECK(problems[2] == ct::BehaviourProblem{5, "has nothing under it"});
    // A script node yet to be given its script.
    const auto unnamed = ct::parseBehaviourTree("sequence\n    script\n", "test", error,
                                                ct::BehaviourReading::Draft);
    REQUIRE(unnamed.has_value());
    CHECK(ct::behaviourProblems(*unnamed) ==
          std::vector<ct::BehaviourProblem>{{1, "names no script"}});
    CHECK(ct::formatBehaviourTree(*unnamed) == "sequence\n    script\n");
    // Written out, a draft reads back the same draft.
    CHECK(ct::parseBehaviourTree(ct::formatBehaviourTree(*draft), "test", error,
                                 ct::BehaviourReading::Draft) == draft);
    CHECK(ct::behaviourProblems(*parse("sequence\n    see\n")).empty());

    // Past the limits, the nodes beyond them have the problem; those past the memory get none.
    std::string waits = "sequence\n";
    for (std::uint32_t i = 0; i <= ct::kMaxBehaviourMemory; ++i) {
        waits += "    wait\n";
    }
    auto many = ct::parseBehaviourTree(waits, "test", error, ct::BehaviourReading::Draft);
    REQUIRE(many.has_value());
    CHECK(many->nodes.back().memory == ct::kNoBehaviourMemory);
    const std::vector<ct::BehaviourProblem> tooMany = ct::behaviourProblems(*many);
    REQUIRE(tooMany.size() == 1);
    CHECK(tooMany[0].node == ct::kMaxBehaviourMemory + 1);

    // Taking a wait away frees a slot; numbering again gives it to the last.
    many->nodes.erase(many->nodes.begin() + 1);
    many->nodes[0].end = static_cast<std::uint16_t>(many->nodes.size());
    for (std::uint32_t i = 1; i < many->nodes.size(); ++i) {
        many->nodes[i].end = static_cast<std::uint16_t>(i + 1);
    }
    ct::numberBehaviourMemory(*many);
    CHECK(many->nodes.back().memory == ct::kMaxBehaviourMemory - 1);
    CHECK(ct::behaviourProblems(*many).empty());
    CHECK(parse(ct::formatBehaviourTree(*many)) == many);
}

TEST_CASE("the shipped standard behaviour is the built-in one") {
    const std::optional<std::string> text = ct::readTextFile(
        std::filesystem::path(CROFTECH_CONTENT_DIR) / "behaviours/standard.behaviour");
    REQUIRE(text.has_value());
    const auto tree = parse(*text);
    REQUIRE(tree.has_value());
    CHECK(tree->nodes == ct::standardBehaviourTree().nodes);
}

TEST_CASE("each enemy type's behaviour is loaded once through the content layers") {
    const ct::test::TempDirectory directory;
    ct::test::writeFile(directory.path() / "engine/behaviours/coward.behaviour", "patrol\n");
    ct::test::writeFile(directory.path() / "game/behaviours/coward.behaviour",
                        "sequence\n    see\n    patrol\n");
    ct::test::writeFile(directory.path() / "game/behaviours/broken.behaviour", "chace\n");
    ct::ContentPaths content;
    content.addLayer(directory.path() / "engine");
    content.addLayer(directory.path() / "game");

    ct::GameSettings settings;
    settings.enemies.resize(5);
    settings.enemies[0].name = "a";
    settings.enemies[0].behaviour = "coward";
    settings.enemies[1].name = "b";
    settings.enemies[1].behaviour = "coward";
    settings.enemies[2].name = "c";
    settings.enemies[2].behaviour = "broken";
    settings.enemies[3].name = "d";
    settings.enemies[3].behaviour = "missing";
    settings.enemies[4].name = "e";
    settings.enemies[4].behaviour = "standard";
    const std::vector<std::string> problems = ct::loadBehaviourTrees(content, settings);
    REQUIRE(problems.size() == 2);
    CHECK(problems[0] == "behaviours/broken.behaviour: line 1: there is no node 'chace'");
    CHECK(problems[1] == "d: there is no behaviours/missing.behaviour");
    // The game's copy wins, and both types share it.
    REQUIRE(settings.behaviours.size() == 1);
    CHECK(settings.behaviours[0].nodes.size() == 3);
    CHECK(settings.enemies[0].behaviourIndex == 0);
    CHECK(settings.enemies[1].behaviourIndex == 0);
    CHECK(&ct::behaviourOf(settings, 1) == &settings.behaviours[0]);
    // The rest use the standard tree, which needs no file.
    CHECK(&ct::behaviourOf(settings, 2) == &ct::standardBehaviourTree());
    CHECK(&ct::behaviourOf(settings, 3) == &ct::standardBehaviourTree());
    CHECK(&ct::behaviourOf(settings, 4) == &ct::standardBehaviourTree());
    CHECK(&ct::behaviourOf(settings, 99) == &ct::standardBehaviourTree());
}

TEST_CASE("every node kind has its name, and only nodes that remember use memory") {
    for (const ct::BehaviourNodeInfo& info : ct::behaviourNodes()) {
        INFO(info.name);
        CHECK(ct::findBehaviourNode(info.name) == &info);
        CHECK(&ct::behaviourNodeInfo(info.kind) == &info);
        if (!info.parameter.name.empty()) {
            CHECK(info.parameter.defaultValue >= info.parameter.minimum);
            CHECK(info.parameter.defaultValue <= info.parameter.maximum);
        }
    }
}

TEST_CASE("the scripts behaviours name are loaded once, and must be behaviour scripts") {
    const ct::test::TempDirectory directory;
    ct::test::writeFile(directory.path() / "game/behaviours/sneaky.behaviour",
                        "fallback\n"
                        "    script flank\n"
                        "    script stateful\n"
                        "    sequence\n"
                        "        script missing\n"
                        "        script flank\n"
                        "    script broken\n"
                        "    patrol\n");
    ct::test::writeFile(directory.path() / "engine/scripts/flank.luau",
                        "return { decide = function(game, me) return true end }");
    ct::test::writeFile(directory.path() / "game/scripts/stateful.luau",
                        "return { state = { n = 0 }, decide = function() return true end }");
    ct::test::writeFile(directory.path() / "game/scripts/broken.luau", "return {");
    ct::ContentPaths content;
    content.addLayer(directory.path() / "engine");
    content.addLayer(directory.path() / "game");

    ct::GameSettings settings;
    settings.enemies.resize(1);
    settings.enemies[0].name = "a";
    settings.enemies[0].behaviour = "sneaky";
    const std::vector<std::string> problems = ct::loadBehaviourTrees(content, settings);
    REQUIRE(problems.size() == 3);
    CHECK(problems[0].starts_with("scripts/broken.luau: broken:1:"));
    CHECK(problems[1] == "there is no scripts/missing.luau, which a behaviour names");
    CHECK(problems[2] == "scripts/stateful.luau: a behaviour's script has only a decide "
                         "function: no state, events, inputs or start");
    CHECK(ct::behaviourScriptNames(settings) ==
          std::vector<std::string>{"broken", "flank", "missing", "stateful"});
    REQUIRE(settings.behaviourScripts.size() == 1);
    CHECK(ct::behaviourScript(settings, "flank") == &settings.behaviourScripts[0]);
    CHECK(ct::behaviourScript(settings, "missing") == nullptr);
    CHECK(ct::behaviourScript(settings, "stateful") == nullptr);

    // Texts being edited stand for the files, and files for the rest.
    const ct::ScriptSource edited[] = {
        {"missing", "return { decide = function(game, me) return false end }"},
        {"broken", "return { decide = function(game, me) return me.health > 1 end }"}};
    CHECK(ct::loadBehaviourScripts(content, settings, edited).size() == 1);
    REQUIRE(settings.behaviourScripts.size() == 3);
    CHECK(settings.behaviourScripts[0].name() == "broken");
    CHECK(settings.behaviourScripts[1].name() == "flank");
    CHECK(settings.behaviourScripts[2].name() == "missing");
}
