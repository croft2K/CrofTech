#include "TestFiles.h"

#include "croftech/core/ConfigFile.h"

#include <doctest/doctest.h>

#include <array>
#include <cstring>
#include <limits>

TEST_CASE("config lines split into key and value, skipping comments and blanks") {
    const auto entries = ct::parseConfig("# header\n"
                                         "\n"
                                         "gravity   20.5   # trailing comment\r\n"
                                         "  name two words\n"
                                         "flag");
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].key == "gravity");
    CHECK(entries[0].value == "20.5");
    CHECK(entries[0].line == 3);
    CHECK(entries[1].key == "name");
    CHECK(entries[1].value == "two words");
    CHECK(entries[2].key == "flag");
    CHECK(entries[2].value.empty());
}

TEST_CASE("parseFloat accepts only a complete finite number") {
    float value = -1.0f;
    CHECK(ct::parseFloat("6.858", value));
    CHECK(value == doctest::Approx(6.858f));
    CHECK_FALSE(ct::parseFloat("6.8x", value));
    CHECK_FALSE(ct::parseFloat("", value));
    CHECK_FALSE(ct::parseFloat("inf", value));
    CHECK_FALSE(ct::parseFloat("nan", value));
}

TEST_CASE("parseFloats needs exactly the expected count of numbers") {
    std::array<float, 3> values{};
    CHECK(ct::parseFloats("1 -2.5\t3", values));
    CHECK(values == std::array{1.0f, -2.5f, 3.0f});
    CHECK(ct::parseFloats("  4 5 6  ", values));
    CHECK_FALSE(ct::parseFloats("1 2", values));
    CHECK_FALSE(ct::parseFloats("1 2 3 4", values));
    CHECK_FALSE(ct::parseFloats("1 2 x", values));
    CHECK_FALSE(ct::parseFloats("1 2 nan", values));
}

TEST_CASE("formatFloat writes the shortest text that reads back exactly") {
    CHECK(ct::formatFloat(0.0f) == "0");
    CHECK(ct::formatFloat(-0.0f) == "0");
    CHECK(ct::formatFloat(1.0f) == "1");
    CHECK(ct::formatFloat(-40.0f) == "-40");
    CHECK(ct::formatFloat(0.1f) == "0.1");
    for (const float value :
         {0.1f, 1.0f / 3.0f, 0.34202015f, -2.2e-7f, 1e30f, std::numeric_limits<float>::max()}) {
        float parsed = 0.0f;
        REQUIRE(ct::parseFloat(ct::formatFloat(value), parsed));
        CHECK(std::memcmp(&parsed, &value, sizeof(float)) == 0);
    }
}

TEST_CASE("parseBool accepts the usual spellings") {
    bool value = false;
    CHECK((ct::parseBool("on", value) && value));
    CHECK((ct::parseBool("0", value) && !value));
    CHECK_FALSE(ct::parseBool("yes", value));
}

TEST_CASE("text files are written whole, replacing what was there") {
    ct::test::TempDirectory directory;
    const std::filesystem::path path = directory.path() / "script.luau";
    std::string error;
    REQUIRE_MESSAGE(ct::writeTextFile(path, "first", error), error);
    REQUIRE_MESSAGE(ct::writeTextFile(path, "second\nversion", error), error);
    CHECK(ct::readTextFile(path) == "second\nversion");
    CHECK_FALSE(std::filesystem::exists(directory.path() / "script.luau.tmp"));
    CHECK_FALSE(ct::writeTextFile(directory.path() / "missing/folder/x.luau", "x", error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("setting a value changes its line in place and keeps the comments") {
    const std::string text = "# Gunner\n"
                             "\n"
                             "health          40      # health it starts with\r\n"
                             "weapon          # empty has no ranged attack\n"
                             "speed 5\n";
    CHECK(ct::setConfigValue(text, "health", "35") ==
          "# Gunner\n"
          "\n"
          "health          35      # health it starts with\r\n"
          "weapon          # empty has no ranged attack\n"
          "speed 5\n");
    // A longer value pushes the comment along rather than into it.
    CHECK(ct::setConfigValue(text, "health", "123456789") ==
          "# Gunner\n"
          "\n"
          "health          123456789  # health it starts with\r\n"
          "weapon          # empty has no ranged attack\n"
          "speed 5\n");
    CHECK(ct::setConfigValue(text, "weapon", "machinegun") ==
          "# Gunner\n"
          "\n"
          "health          40      # health it starts with\r\n"
          "weapon          machinegun  # empty has no ranged attack\n"
          "speed 5\n");
    CHECK(ct::setConfigValue(text, "health", "") ==
          "# Gunner\n"
          "\n"
          "health                  # health it starts with\r\n"
          "weapon          # empty has no ranged attack\n"
          "speed 5\n");
    CHECK(ct::setConfigValue(text, "speed", " 6.5 ").ends_with("speed 6.5\n"));
}

TEST_CASE("setting a value adds a line for a new key and changes only the last of repeats") {
    CHECK(ct::setConfigValue("", "health", "40") == "health          40\n");
    CHECK(ct::setConfigValue("a 1", "health", "40") == "a 1\nhealth          40\n");
    CHECK(ct::setConfigValue("a 1\r\n", "b", "2") == "a 1\r\nb               2\r\n");
    CHECK(ct::setConfigValue("a 1\na 2\n", "a", "3") == "a 1\na 3\n");
    const std::string set = ct::setConfigValue("x 1\n", "description", "Shoots back");
    const auto entries = ct::parseConfig(set);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1].key == "description");
    CHECK(entries[1].value == "Shoots back");
}

TEST_CASE("removing a value drops every line of its key") {
    CHECK(ct::removeConfigValue("# keep\na 1   # one\nb 2\na 3\n", "a") == "# keep\nb 2\n");
    CHECK(ct::removeConfigValue("a 1", "b") == "a 1");
}

TEST_CASE("config values are one line with no comment") {
    CHECK(ct::isConfigValue("A gunner that misses"));
    CHECK(ct::isConfigValue(""));
    CHECK_FALSE(ct::isConfigValue("number #1"));
    CHECK_FALSE(ct::isConfigValue("two\nlines"));
}
