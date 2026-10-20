#include "croftech/core/CVar.h"

#include <doctest/doctest.h>

TEST_CASE("float variables accept only numbers in range") {
    ct::CVar fov = ct::CVar::makeFloat("fov", 100.0f, 60.0f, 140.0f, "");
    CHECK(fov.text() == "100");
    CHECK(fov.isDefault());

    CHECK(fov.set("110.5"));
    CHECK(fov.asFloat() == 110.5f);
    CHECK(fov.text() == "110.5");
    CHECK_FALSE(fov.isDefault());

    CHECK_FALSE(fov.set("150"));
    CHECK_FALSE(fov.set("wide"));
    CHECK_FALSE(fov.set(""));
    CHECK_FALSE(fov.set("inf"));
    CHECK(fov.asFloat() == 110.5f);

    fov.reset();
    CHECK(fov.asFloat() == 100.0f);
    CHECK(fov.describeRange() == "a number from 60 to 140");
}

TEST_CASE("int, bool and string variables") {
    ct::CVar count = ct::CVar::makeInt("count", 3, 0, 10, "");
    CHECK(count.set("7"));
    CHECK(count.asInt() == 7);
    CHECK_FALSE(count.set("7.5"));
    CHECK_FALSE(count.set("11"));
    CHECK(count.describeRange() == "a whole number from 0 to 10");

    ct::CVar flag = ct::CVar::makeBool("flag", false, "");
    CHECK(flag.text() == "0");
    CHECK(flag.set("on"));
    CHECK(flag.asBool());
    CHECK(flag.text() == "1");
    CHECK_FALSE(flag.set("maybe"));
    CHECK(flag.asBool());

    ct::CVar name = ct::CVar::makeString("name", "player", "");
    CHECK(name.set("Ranger Rick"));
    CHECK(name.text() == "Ranger Rick");
}

TEST_CASE("open-ended ranges describe themselves") {
    const ct::CVar gravity = ct::CVar::makeFloat("g", 20.0f, 0.0f, ct::CVar::kNoLimit, "");
    CHECK(gravity.describeRange() == "a number of at least 0");
}
