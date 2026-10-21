// Fuzzes loading saved simulation states. Anything that loads must save, load again and give the
// same state.

#include "croftech/sim/Simulation.h"
#include "croftech/sim/StateSave.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = std::as_bytes(std::span{data, size});
    auto state = std::make_unique<ct::State>();
    std::string error;
    if (!ct::loadState(bytes, *state, error)) {
        return 0;
    }
    const std::vector<std::byte> saved = ct::saveState(*state);
    auto reloaded = std::make_unique<ct::State>();
    if (!ct::loadState(saved, *reloaded, error) ||
        ct::hashState(*reloaded) != ct::hashState(*state) || ct::saveState(*reloaded) != saved) {
        std::abort();
    }
    return 0;
}
