// Fuzzes reading network messages, as a server reads client packets and a client reads snapshots.
// A client message that reads must write back to the same bytes; a snapshot that reads must have
// state that is either rejected or loads, saves and loads again unchanged.

#include "croftech/net/Protocol.h"
#include "croftech/sim/StateSave.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = std::as_bytes(std::span{data, size});
    if (const auto message = ct::readClientMessage(bytes)) {
        ct::ByteWriter writer;
        ct::writeClientMessage(writer, *message);
        const auto& written = writer.bytes();
        if (written.size() != bytes.size() ||
            !std::equal(written.begin(), written.end(), bytes.begin())) {
            std::abort();
        }
    }
    if (const auto snapshot = ct::readSnapshot(bytes)) {
        auto state = std::make_unique<ct::State>();
        std::string error;
        if (ct::loadState(snapshot->state, *state, error)) {
            auto again = std::make_unique<ct::State>();
            if (!ct::loadState(ct::saveState(*state), *again, error) ||
                ct::saveState(*again) != ct::saveState(*state)) {
                std::abort();
            }
        }
    }
    return 0;
}
