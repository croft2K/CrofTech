#include "croftech/core/ConfigFile.h"

#include <doctest/doctest.h>

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

TEST_CASE("parseBool accepts the usual spellings") {
    bool value = false;
    CHECK((ct::parseBool("on", value) && value));
    CHECK((ct::parseBool("0", value) && !value));
    CHECK_FALSE(ct::parseBool("yes", value));
}
