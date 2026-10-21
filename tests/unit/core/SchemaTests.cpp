#include "croftech/core/ByteStream.h"
#include "croftech/core/Schema.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream> // doctest prints string_view through operator<<
#include <span>

namespace {

struct Sample {
    ct::Vec3 position;
    float speed = 0.0f;
    std::int32_t count = 0;
    std::uint16_t yaw = 0;
    std::int16_t pitch = 0;
};

constexpr ct::FieldInfo kSampleFields[] = {
    CT_FIELD(Sample, position).withHint(ct::FieldHint::Distance),
    CT_FIELD(Sample, speed),
    CT_FIELD(Sample, count).withRange(-5, 5),
    CT_FIELD(Sample, yaw).withHint(ct::FieldHint::Angle),
    CT_FIELD(Sample, pitch).withHint(ct::FieldHint::Angle),
};
constexpr ct::TypeSchema kSample{"sample", sizeof(Sample), kSampleFields};
static_assert(ct::describesEveryByte(kSample));

// A field left out of the schema.
constexpr ct::TypeSchema kMissingField{"sample", sizeof(Sample), std::span{kSampleFields}.first(4)};
static_assert(!ct::describesEveryByte(kMissingField));

struct Padded {
    std::uint8_t small = 0;
    std::uint32_t large = 0;
};
constexpr ct::FieldInfo kPaddedFields[] = {CT_FIELD(Padded, small), CT_FIELD(Padded, large)};
static_assert(!ct::describesEveryByte({"padded", sizeof(Padded), kPaddedFields}));

} // namespace

TEST_CASE("CT_FIELD records name, type, offset and options") {
    CHECK(kSampleFields[0].name == "position");
    CHECK(kSampleFields[0].type == ct::FieldType::Vec3);
    CHECK(kSampleFields[0].offset == 0);
    CHECK(kSampleFields[0].options.hint == ct::FieldHint::Distance);
    CHECK(kSampleFields[2].type == ct::FieldType::I32);
    CHECK(kSampleFields[2].offset == 16);
    CHECK(kSampleFields[2].options.max == 5);
    CHECK(kSampleFields[4].type == ct::FieldType::I16);
}

TEST_CASE("fields encode little-endian and decode to the same bytes") {
    Sample sample{{1.5f, -2.0f, 3.25f}, -0.0f, -3, 0xBEEF, -1234};
    const auto in = std::as_bytes(std::span{&sample, 1});
    ct::ByteWriter writer;
    for (const ct::FieldInfo& field : kSample.fields) {
        ct::encodeField(writer, field, in);
    }
    REQUIRE(writer.bytes().size() == sizeof(Sample));
    CHECK(writer.bytes()[20] == std::byte{0xEF}); // yaw, low byte first
    CHECK(writer.bytes()[21] == std::byte{0xBE});

    Sample copy;
    const auto out = std::as_writable_bytes(std::span{&copy, 1});
    ct::ByteReader reader(writer.bytes());
    for (const ct::FieldInfo& field : kSample.fields) {
        ct::decodeField(reader, field, out);
    }
    CHECK(reader.ok());
    CHECK(reader.atEnd());
    CHECK(copy.position == sample.position);
    CHECK(std::signbit(copy.speed));
    CHECK(copy.count == -3);
    CHECK(copy.yaw == 0xBEEF);
    CHECK(copy.pitch == -1234);
}

TEST_CASE("loaded values must be finite and inside the field's range") {
    Sample sample;
    const auto bytes = std::as_writable_bytes(std::span{&sample, 1});
    CHECK(ct::isValidFieldValue(kSampleFields[2], bytes));
    sample.count = 6;
    CHECK_FALSE(ct::isValidFieldValue(kSampleFields[2], bytes));
    sample.count = -5;
    CHECK(ct::isValidFieldValue(kSampleFields[2], bytes));
    sample.speed = std::numeric_limits<float>::infinity();
    CHECK_FALSE(ct::isValidFieldValue(kSampleFields[1], bytes));
    sample.position.y = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(ct::isValidFieldValue(kSampleFields[0], bytes));
}

TEST_CASE("byte readers fail instead of reading past the end") {
    ct::ByteWriter writer;
    writer.writeU16(0x1234);
    writer.writeShortString("abc");
    ct::ByteReader reader(writer.bytes());
    CHECK(reader.readU16() == 0x1234);
    CHECK(reader.readShortString() == "abc");
    CHECK(reader.ok());
    CHECK(reader.readU32() == 0);
    CHECK_FALSE(reader.ok());
    CHECK(reader.readU8() == 0); // stays failed

    const std::byte longString[] = {std::byte{10}, std::byte{'a'}};
    ct::ByteReader truncated(longString);
    CHECK(truncated.readShortString().empty());
    CHECK_FALSE(truncated.ok());
}
