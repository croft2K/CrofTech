#include "croftech/core/ByteStream.h"
#include "croftech/sim/SaveGame.h"
#include "croftech/sim/StateSave.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

ct::SaveGame sampleSave() {
    auto state = std::make_unique<ct::State>();
    state->tick = 99;
    return {"e1m1", 0x0123456789ABCDEFull, ct::saveState(*state)};
}

std::string rejection(const std::vector<std::byte>& bytes) {
    ct::SaveGame save;
    std::string error;
    CHECK_FALSE(ct::readSaveGame(bytes, save, error));
    return error;
}

} // namespace

TEST_CASE("a save game records its level and state, and reads back the same") {
    const ct::SaveGame save = sampleSave();
    const std::vector<std::byte> bytes = ct::writeSaveGame(save);
    ct::SaveGame read;
    std::string error;
    REQUIRE_MESSAGE(ct::readSaveGame(bytes, read, error), error);
    CHECK(read == save);
    auto state = std::make_unique<ct::State>();
    REQUIRE_MESSAGE(ct::loadState(read.state, *state, error), error);
    CHECK(state->tick == 99);
}

TEST_CASE("files that aren't whole saves are refused") {
    const std::vector<std::byte> good = ct::writeSaveGame(sampleSave());

    std::vector<std::byte> notSave = good;
    notSave[0] = std::byte{'X'};
    CHECK(rejection(notSave) == "not a CrofTech save");

    std::vector<std::byte> newer = good;
    newer[4] = std::byte{2};
    CHECK(rejection(newer) == "unsupported save version 2");

    const std::vector<std::byte> truncatedName(good.begin(), good.begin() + 12);
    CHECK(rejection(truncatedName) == "the save's level name is missing or truncated");

    ct::ByteWriter noName;
    noName.writeU32(0x47535443);
    noName.writeU32(ct::kSaveGameVersion);
    noName.writeU16(0);
    noName.writeU64(0);
    noName.writeU32(1);
    CHECK(rejection(noName.bytes()) == "the save's level name is missing or truncated");

    // Everything but the state.
    const std::vector<std::byte> noState(good.begin(), good.begin() + 4 + 4 + 2 + 4 + 8);
    CHECK(rejection(noState) == "the save has no state");
}
