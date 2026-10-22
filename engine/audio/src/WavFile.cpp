#include "croftech/audio/WavFile.h"

#include "croftech/core/ByteStream.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>

namespace ct {
namespace {

constexpr std::uint16_t kFormatPcm = 1;
constexpr std::uint16_t kFormatFloat = 3;
constexpr std::uint16_t kFormatExtensible = 0xFFFE;
// Files with more chunks than this are rejected rather than walked for ever.
constexpr int kMaxChunks = 1024;

bool isTag(std::span<const std::byte> bytes, std::string_view tag) {
    return bytes.size() == 4 &&
           std::equal(tag.begin(), tag.end(), bytes.begin(),
                      [](char c, std::byte b) { return static_cast<std::byte>(c) == b; });
}

struct Format {
    std::uint16_t tag = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bits = 0;
};

float decodeSample(std::span<const std::byte> sample, const Format& format) {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint32_t>(sample[i]); };
    if (format.tag == kFormatFloat) {
        const float value =
            std::bit_cast<float>(byte(0) | byte(1) << 8 | byte(2) << 16 | byte(3) << 24);
        // NaN and infinities from a broken or hostile file become silence and full scale.
        return std::isnan(value) ? 0.0f : std::clamp(value, -1.0f, 1.0f);
    }
    switch (format.bits) {
    case 8: // unsigned
        return (static_cast<float>(byte(0)) - 128.0f) / 128.0f;
    case 16:
        return static_cast<float>(static_cast<std::int16_t>(byte(0) | byte(1) << 8)) / 32768.0f;
    case 24: {
        const auto value =
            static_cast<std::int32_t>((byte(0) << 8 | byte(1) << 16 | byte(2) << 24));
        return static_cast<float>(value >> 8) / 8388608.0f;
    }
    default: {
        const auto value =
            static_cast<std::int32_t>(byte(0) | byte(1) << 8 | byte(2) << 16 | byte(3) << 24);
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    }
}

} // namespace

std::optional<Sound> parseWav(std::span<const std::byte> bytes, std::string& error) {
    const auto fail = [&error](std::string message) -> std::optional<Sound> {
        error = std::move(message);
        return std::nullopt;
    };
    if (bytes.size() > kMaxWavBytes) {
        return fail(std::format("larger than {} MiB", kMaxWavBytes >> 20));
    }
    ByteReader reader(bytes);
    const std::span<const std::byte> riff = reader.readBytes(4);
    reader.readU32(); // the RIFF size, often wrong in the wild; the chunks are checked instead
    const std::span<const std::byte> wave = reader.readBytes(4);
    if (!reader.ok() || !isTag(riff, "RIFF") || !isTag(wave, "WAVE")) {
        return fail("not a WAV file");
    }

    std::optional<Format> format;
    std::span<const std::byte> data;
    bool haveData = false;
    for (int chunk = 0; chunk < kMaxChunks && reader.remaining() >= 8; ++chunk) {
        const std::span<const std::byte> id = reader.readBytes(4);
        const std::uint32_t size = reader.readU32();
        // A last chunk that claims more than the file holds is cut short, as many writers leave
        // the data size unfinished.
        const std::span<const std::byte> body =
            reader.readBytes(std::min<std::size_t>(size, reader.remaining()));
        if (size % 2 == 1 && reader.remaining() > 0) {
            reader.readBytes(1); // chunks are padded to an even size
        }
        if (isTag(id, "fmt ")) {
            if (body.size() < 16) {
                return fail("format chunk is too short");
            }
            ByteReader fields(body);
            Format read;
            read.tag = fields.readU16();
            read.channels = fields.readU16();
            read.sampleRate = fields.readU32();
            fields.readU32(); // bytes per second, derived from the rest
            read.blockAlign = fields.readU16();
            read.bits = fields.readU16();
            if (read.tag == kFormatExtensible) {
                if (body.size() < 40) {
                    return fail("extensible format chunk is too short");
                }
                fields.readU16();            // extra size
                fields.readU16();            // valid bits
                fields.readU32();            // speaker positions
                read.tag = fields.readU16(); // the first part of the sub-format's GUID
            }
            format = read;
        } else if (isTag(id, "data") && !haveData) {
            data = body;
            haveData = true;
        }
    }
    if (!format) {
        return fail("no format chunk");
    }
    if (!haveData) {
        return fail("no data chunk");
    }
    const Format& f = *format;
    const bool pcm =
        f.tag == kFormatPcm && (f.bits == 8 || f.bits == 16 || f.bits == 24 || f.bits == 32);
    const bool floating = f.tag == kFormatFloat && f.bits == 32;
    if (!pcm && !floating) {
        return fail("unsupported encoding (only 8 to 32-bit PCM and 32-bit float)");
    }
    if (f.channels < 1 || f.channels > 2) {
        return fail("only mono and stereo are supported");
    }
    if (f.sampleRate < kMinSampleRate || f.sampleRate > kMaxSampleRate) {
        return fail(std::format("sample rate must be {} to {} Hz", kMinSampleRate, kMaxSampleRate));
    }
    const std::size_t bytesPerSample = f.bits / 8u;
    if (f.blockAlign != bytesPerSample * f.channels) {
        return fail("block size doesn't match the channels and bits");
    }
    const std::size_t frames = data.size() / f.blockAlign;
    if (frames * f.channels > kMaxSoundSamples) {
        return fail("too long");
    }

    Sound sound;
    sound.sampleRate = f.sampleRate;
    sound.channels = f.channels;
    sound.samples.resize(frames * f.channels);
    for (std::size_t i = 0; i < sound.samples.size(); ++i) {
        sound.samples[i] = decodeSample(data.subspan(i * bytesPerSample, bytesPerSample), f);
    }
    return sound;
}

} // namespace ct
