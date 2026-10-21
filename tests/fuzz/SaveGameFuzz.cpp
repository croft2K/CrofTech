// Fuzzes reading save game files. A save that reads must write back to the same bytes, and its
// state must either be rejected or load.

#include "croftech/sim/SaveGame.h"
#include "croftech/sim/StateSave.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = std::as_bytes(std::span{data, size});
    ct::SaveGame save;
    std::string error;
    if (!ct::readSaveGame(bytes, save, error)) {
        return 0;
    }
    const std::vector<std::byte> written = ct::writeSaveGame(save);
    if (!std::equal(written.begin(), written.end(), bytes.begin(), bytes.end())) {
        std::abort();
    }
    auto state = std::make_unique<ct::State>();
    ct::loadState(save.state, *state, error);
    return 0;
}
