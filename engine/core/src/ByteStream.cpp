#include "croftech/core/ByteStream.h"

#include "croftech/core/Assert.h"

namespace ct {

void ByteWriter::writeShortString(std::string_view text) {
    CT_ASSERT(text.size() <= 255);
    const std::size_t length = text.size() <= 255 ? text.size() : 255;
    writeU8(static_cast<std::uint8_t>(length));
    writeBytes(std::as_bytes(std::span{text.data(), length}));
}

std::string_view ByteReader::readShortString() {
    const std::size_t length = readU8();
    const std::span<const std::byte> bytes = readBytes(length);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::span<const std::byte> ByteReader::readBytes(std::size_t count) {
    if (m_failed || count > remaining()) {
        m_failed = true;
        return {};
    }
    const std::span<const std::byte> bytes = m_bytes.subspan(m_position, count);
    m_position += count;
    return bytes;
}

std::uint64_t ByteReader::readLittleEndian(int count) {
    const std::span<const std::byte> bytes = readBytes(static_cast<std::size_t>(count));
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
}

} // namespace ct
