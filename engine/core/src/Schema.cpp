#include "croftech/core/Schema.h"

#include "croftech/core/Assert.h"
#include "croftech/core/ByteStream.h"

#include <cmath>
#include <cstring>

namespace ct {
namespace {

template <typename T>
T load(std::span<const std::byte> object, std::uint32_t offset) {
    CT_ASSERT(offset + sizeof(T) <= object.size());
    T value;
    std::memcpy(&value, object.data() + offset, sizeof(T));
    return value;
}

template <typename T>
void store(std::span<std::byte> object, std::uint32_t offset, T value) {
    CT_ASSERT(offset + sizeof(T) <= object.size());
    std::memcpy(object.data() + offset, &value, sizeof(T));
}

} // namespace

std::string_view fieldTypeName(FieldType type) noexcept {
    switch (type) {
    case FieldType::Float:
        return "float";
    case FieldType::Vec3:
        return "vec3";
    case FieldType::U8:
        return "u8";
    case FieldType::U16:
        return "u16";
    case FieldType::U32:
        return "u32";
    case FieldType::I16:
        return "i16";
    case FieldType::I32:
        return "i32";
    }
    return "unknown";
}

bool isValidFieldValue(const FieldInfo& field, std::span<const std::byte> object) {
    switch (field.type) {
    case FieldType::Float:
        return std::isfinite(readFloat(object, field.offset));
    case FieldType::Vec3: {
        const Vec3 v = readVec3(object, field.offset);
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
    default: {
        const std::int64_t value = readInteger(field, object);
        return value >= field.options.min && value <= field.options.max;
    }
    }
}

float readFloat(std::span<const std::byte> object, std::uint32_t offset) {
    return load<float>(object, offset);
}

Vec3 readVec3(std::span<const std::byte> object, std::uint32_t offset) {
    return {load<float>(object, offset), load<float>(object, offset + 4),
            load<float>(object, offset + 8)};
}

std::int64_t readInteger(const FieldInfo& field, std::span<const std::byte> object) {
    switch (field.type) {
    case FieldType::U8:
        return load<std::uint8_t>(object, field.offset);
    case FieldType::U16:
        return load<std::uint16_t>(object, field.offset);
    case FieldType::U32:
        return load<std::uint32_t>(object, field.offset);
    case FieldType::I16:
        return load<std::int16_t>(object, field.offset);
    case FieldType::I32:
        return load<std::int32_t>(object, field.offset);
    case FieldType::Float:
    case FieldType::Vec3:
        break;
    }
    CT_ASSERT(false && "not an integer field");
    return 0;
}

void writeFloat(std::span<std::byte> object, std::uint32_t offset, float value) {
    store(object, offset, value);
}

void writeVec3(std::span<std::byte> object, std::uint32_t offset, Vec3 value) {
    store(object, offset, value.x);
    store(object, offset + 4, value.y);
    store(object, offset + 8, value.z);
}

// Values outside the field's storage range wrap, as a C++ conversion would.
void writeInteger(const FieldInfo& field, std::span<std::byte> object, std::int64_t value) {
    switch (field.type) {
    case FieldType::U8:
        store(object, field.offset, static_cast<std::uint8_t>(value));
        return;
    case FieldType::U16:
        store(object, field.offset, static_cast<std::uint16_t>(value));
        return;
    case FieldType::U32:
        store(object, field.offset, static_cast<std::uint32_t>(value));
        return;
    case FieldType::I16:
        store(object, field.offset, static_cast<std::int16_t>(value));
        return;
    case FieldType::I32:
        store(object, field.offset, static_cast<std::int32_t>(value));
        return;
    case FieldType::Float:
    case FieldType::Vec3:
        break;
    }
    CT_ASSERT(false && "not an integer field");
}

void encodeField(ByteWriter& writer, const FieldInfo& field, std::span<const std::byte> object) {
    switch (field.type) {
    case FieldType::Float:
        writer.writeF32(readFloat(object, field.offset));
        return;
    case FieldType::Vec3: {
        const Vec3 v = readVec3(object, field.offset);
        writer.writeF32(v.x);
        writer.writeF32(v.y);
        writer.writeF32(v.z);
        return;
    }
    case FieldType::U8:
        writer.writeU8(static_cast<std::uint8_t>(readInteger(field, object)));
        return;
    case FieldType::U16:
    case FieldType::I16:
        writer.writeU16(static_cast<std::uint16_t>(readInteger(field, object)));
        return;
    case FieldType::U32:
    case FieldType::I32:
        writer.writeU32(static_cast<std::uint32_t>(readInteger(field, object)));
        return;
    }
}

void decodeField(ByteReader& reader, const FieldInfo& field, std::span<std::byte> object) {
    switch (field.type) {
    case FieldType::Float:
        writeFloat(object, field.offset, reader.readF32());
        return;
    case FieldType::Vec3: {
        const float x = reader.readF32();
        const float y = reader.readF32();
        const float z = reader.readF32();
        writeVec3(object, field.offset, {x, y, z});
        return;
    }
    case FieldType::U8:
        writeInteger(field, object, reader.readU8());
        return;
    case FieldType::U16:
        writeInteger(field, object, reader.readU16());
        return;
    case FieldType::I16:
        writeInteger(field, object, static_cast<std::int16_t>(reader.readU16()));
        return;
    case FieldType::U32:
        writeInteger(field, object, reader.readU32());
        return;
    case FieldType::I32:
        writeInteger(field, object, static_cast<std::int32_t>(reader.readU32()));
        return;
    }
}

} // namespace ct
