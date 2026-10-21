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
    CHECK(ct::parseFloats("1 -2.5	3", values));
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
