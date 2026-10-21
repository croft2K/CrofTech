#include "croftech/sim/SaveGame.h"

#include "croftech/core/Assert.h"
#include "croftech/core/ByteStream.h"

#include <format>
#include <string_view>

namespace ct {
namespace {

constexpr std::uint32_t kMagic = 0x47535443; // "CTSG" in file order

} // namespace

std::vector<std::byte> writeSaveGame(const SaveGame& save) {
    CT_ASSERT(!save.level.empty() && save.level.size() <= kMaxSaveLevelName);
    ByteWriter writer;
    writer.writeU32(kMagic);
    writer.writeU32(kSaveGameVersion);
    writer.writeU16(static_cast<std::uint16_t>(save.level.size()));
    writer.writeBytes(std::as_bytes(std::span{save.level}));
    writer.writeU64(save.levelHash);
    writer.writeBytes(save.state);
    return writer.take();
}

bool readSaveGame(std::span<const std::byte> bytes, SaveGame& save, std::string& error) {
    ByteReader reader(bytes);
    if (reader.readU32() != kMagic) {
        error = "not a CrofTech save";
        return false;
    }
    if (const std::uint32_t version = reader.readU32(); version != kSaveGameVersion) {
        error = std::format("unsupported save version {}", version);
        return false;
    }
    const std::uint16_t nameLength = reader.readU16();
    const std::span<const std::byte> name = reader.readBytes(nameLength);
    const std::uint64_t levelHash = reader.readU64();
    if (!reader.ok() || nameLength == 0 || nameLength > kMaxSaveLevelName) {
        error = "the save's level name is missing or truncated";
        return false;
    }
    const std::span<const std::byte> state = reader.readBytes(reader.remaining());
    if (state.empty()) {
        error = "the save has no state";
        return false;
    }
    save.level.assign(reinterpret_cast<const char*>(name.data()), name.size());
    save.levelHash = levelHash;
    save.state.assign(state.begin(), state.end());
    return true;
}

} // namespace ct
