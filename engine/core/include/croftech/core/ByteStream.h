#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace ct {

// Little-endian binary encoding for files such as saves. Values are written byte by byte, so the
// bytes are the same on every platform. Network packets will use a bit-level stream instead.
class ByteWriter {
public:
    void writeU8(std::uint8_t value) { m_bytes.push_back(static_cast<std::byte>(value)); }
    void writeU16(std::uint16_t value) { writeLittleEndian(value, 2); }
    void writeU32(std::uint32_t value) { writeLittleEndian(value, 4); }
    void writeU64(std::uint64_t value) { writeLittleEndian(value, 8); }
    void writeF32(float value) { writeU32(std::bit_cast<std::uint32_t>(value)); }
    // A length byte then the characters; at most 255 of them.
    void writeShortString(std::string_view text);
    void writeBytes(std::span<const std::byte> bytes) {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    const std::vector<std::byte>& bytes() const { return m_bytes; }
    // Empties the writer but keeps its memory, so a writer reused every tick doesn't allocate.
    void clear() { m_bytes.clear(); }
    std::vector<std::byte> take() { return std::move(m_bytes); }

private:
    void writeLittleEndian(std::uint64_t value, int count) {
        for (int i = 0; i < count; ++i) {
            m_bytes.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
        }
    }

    std::vector<std::byte> m_bytes;
};

// Reads untrusted bytes. Every read is bounds-checked: reading past the end returns zero and
// marks the reader failed, so a parser can read a whole record and check ok() once.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    std::uint8_t readU8() { return static_cast<std::uint8_t>(readLittleEndian(1)); }
    std::uint16_t readU16() { return static_cast<std::uint16_t>(readLittleEndian(2)); }
    std::uint32_t readU32() { return static_cast<std::uint32_t>(readLittleEndian(4)); }
    std::uint64_t readU64() { return readLittleEndian(8); }
    float readF32() { return std::bit_cast<float>(readU32()); }
    std::string_view readShortString();
    // Advances past count bytes and returns them; empty if there are not that many.
    std::span<const std::byte> readBytes(std::size_t count);

    bool ok() const { return !m_failed; }
    bool atEnd() const { return m_position == m_bytes.size(); }
    std::size_t remaining() const { return m_bytes.size() - m_position; }

private:
    std::uint64_t readLittleEndian(int count);

    std::span<const std::byte> m_bytes;
    std::size_t m_position = 0;
    bool m_failed = false;
};

} // namespace ct
