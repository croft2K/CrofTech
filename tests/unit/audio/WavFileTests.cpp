#include "croftech/audio/WavFile.h"
#include "croftech/core/ConfigFile.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Bytes {
    std::vector<std::byte> data;

    void tag(std::string_view text) {
        for (const char c : text) {
            data.push_back(static_cast<std::byte>(c));
        }
    }
    void u16(std::uint32_t value) {
        data.push_back(static_cast<std::byte>(value & 0xff));
        data.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    }
    void u32(std::uint32_t value) {
        u16(value & 0xffff);
        u16(value >> 16);
    }
    void raw(std::initializer_list<std::uint8_t> values) {
        for (const std::uint8_t value : values) {
            data.push_back(static_cast<std::byte>(value));
        }
    }
};

struct Format {
    std::uint16_t tag = 1;
    std::uint16_t channels = 1;
    std::uint32_t rate = 22050;
    std::uint16_t bits = 16;
    bool extensible = false;
};

// A WAV file with the format and the given sample bytes, plus an extra chunk before the data.
std::vector<std::byte> wav(const Format& format, std::initializer_list<std::uint8_t> samples,
                           bool extraChunk = false) {
    Bytes out;
    out.tag("RIFF");
    out.u32(0); // writers often leave this wrong; the reader doesn't rely on it
    out.tag("WAVE");
    out.tag("fmt ");
    out.u32(format.extensible ? 40 : 16);
    out.u16(format.extensible ? 0xFFFE : format.tag);
    out.u16(format.channels);
    out.u32(format.rate);
    const std::uint16_t block = static_cast<std::uint16_t>(format.channels * format.bits / 8);
    out.u32(format.rate * block);
    out.u16(block);
    out.u16(format.bits);
    if (format.extensible) {
        out.u16(22);
        out.u16(format.bits);
        out.u32(0);
        out.u16(format.tag);
        out.raw({0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71});
    }
    if (extraChunk) {
        out.tag("LIST");
        out.u32(3); // odd, so padded
        out.raw({1, 2, 3, 0});
    }
    out.tag("data");
    out.u32(static_cast<std::uint32_t>(samples.size()));
    out.raw(samples);
    return out.data;
}

std::string failure(const std::vector<std::byte>& bytes) {
    std::string error;
    CHECK_FALSE(ct::parseWav(bytes, error).has_value());
    return error;
}

} // namespace

TEST_CASE("16-bit mono WAV files decode to samples from -1 to 1") {
    std::string error;
    const std::optional<ct::Sound> sound =
        ct::parseWav(wav({}, {0x00, 0x00, 0xff, 0x7f, 0x00, 0x80, 0x00, 0x40}, true), error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->sampleRate == 22050);
    CHECK(sound->channels == 1);
    REQUIRE(sound->samples.size() == 4);
    CHECK(sound->samples[0] == 0.0f);
    CHECK(sound->samples[1] == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(sound->samples[2] == -1.0f);
    CHECK(sound->samples[3] == 0.5f);
}

TEST_CASE("8, 24 and 32-bit PCM, float and the extensible format decode") {
    std::string error;
    Format eight;
    eight.bits = 8;
    std::optional<ct::Sound> sound = ct::parseWav(wav(eight, {128, 0, 255}), error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->samples == std::vector<float>{0.0f, -1.0f, 127.0f / 128.0f});

    Format twentyFour;
    twentyFour.bits = 24;
    sound = ct::parseWav(wav(twentyFour, {0x00, 0x00, 0xc0, 0x00, 0x00, 0x40}), error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->samples == std::vector<float>{-0.5f, 0.5f});

    Format stereoFloat;
    stereoFloat.tag = 3;
    stereoFloat.bits = 32;
    stereoFloat.channels = 2;
    stereoFloat.extensible = true;
    sound = ct::parseWav(wav(stereoFloat, {0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0xbf}), error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->channels == 2);
    CHECK(sound->frameCount() == 1);
    CHECK(sound->samples == std::vector<float>{0.5f, -1.0f});

    Format thirtyTwo;
    thirtyTwo.bits = 32;
    sound = ct::parseWav(wav(thirtyTwo, {0x00, 0x00, 0x00, 0xc0}), error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->samples == std::vector<float>{-0.5f});
}

TEST_CASE("broken float samples become silence or full scale") {
    Format floating;
    floating.tag = 3;
    floating.bits = 32;
    std::string error;
    const std::optional<ct::Sound> sound = ct::parseWav(
        wav(floating, {0x00, 0x00, 0xc0, 0x7f, 0x00, 0x00, 0x80, 0x7f, 0x00, 0x00, 0x20, 0x41}),
        error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->samples == std::vector<float>{0.0f, 1.0f, 1.0f});
}

TEST_CASE("a data chunk that claims more than the file holds is cut short") {
    std::vector<std::byte> bytes = wav({}, {0x00, 0x40, 0x00, 0x40});
    bytes.pop_back(); // half a sample left over
    std::string error;
    const std::optional<ct::Sound> sound = ct::parseWav(bytes, error);
    REQUIRE_MESSAGE(sound.has_value(), error);
    CHECK(sound->samples.size() == 1);
}

TEST_CASE("malformed and unsupported WAV files are rejected") {
    CHECK(failure({}) == "not a WAV file");
    std::vector<std::byte> noFormat = wav({}, {0, 0});
    noFormat[12] = std::byte{'x'};
    CHECK(failure(noFormat) == "no format chunk");
    Format three;
    three.channels = 3;
    CHECK(failure(wav(three, {0, 0, 0, 0, 0, 0})) == "only mono and stereo are supported");
    Format slow;
    slow.rate = 4000;
    CHECK(failure(wav(slow, {0, 0})).starts_with("sample rate"));
    Format twelve;
    twelve.bits = 12;
    CHECK(failure(wav(twelve, {0, 0})).starts_with("unsupported encoding"));
    Format compressed;
    compressed.tag = 2;
    CHECK(failure(wav(compressed, {0, 0})).starts_with("unsupported encoding"));
    std::vector<std::byte> badBlock = wav({}, {0, 0});
    badBlock[32] = std::byte{3};
    CHECK(failure(badBlock) == "block size doesn't match the channels and bits");
    std::vector<std::byte> noData = wav({}, {0, 0});
    noData.resize(36);
    CHECK(failure(noData) == "no data chunk");
}

TEST_CASE("the shipped placeholder sounds all load") {
    int count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(CROFTECH_CONTENT_DIR "/sounds")) {
        const std::optional<std::string> text = ct::readTextFile(entry.path(), ct::kMaxWavBytes);
        REQUIRE(text.has_value());
        std::string error;
        const std::optional<ct::Sound> sound = ct::parseWav(std::as_bytes(std::span{*text}), error);
        const std::string problem = entry.path().string() + ": " + error;
        REQUIRE_MESSAGE(sound.has_value(), problem);
        CHECK(sound->frameCount() > 0);
        ++count;
    }
    CHECK(count >= 10);
}
