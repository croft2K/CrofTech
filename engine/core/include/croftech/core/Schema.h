#pragma once

#include "croftech/core/Math.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace ct {

// A schema describes a plain-data struct field by field: name, storage type, byte offset and how
// to present it. Each struct is described once, next to its definition, and generic code works
// from the description: the editor inspector, saves and, later, network replication. No code
// outside the struct's own file names its fields.

// Entity is a reference to an entity: its index then its generation, two u32s (sim's EntityId).
enum class FieldType : std::uint8_t { Float, Vec3, U8, U16, U32, I16, I32, Entity };

// How tools show a field; storage is the same either way.
enum class FieldHint : std::uint8_t {
    None,
    Distance, // metres
    Speed,    // metres per second
    Angle,    // 16-bit turn fraction (65536 = 360 degrees), shown in degrees
    Flags,    // bit set; FieldOptions::flagNames names the bits
};

struct FieldOptions {
    FieldHint hint = FieldHint::None;
    std::string_view description;
    std::span<const std::string_view> flagNames; // bit 0 first
    // Inclusive range for integer fields, checked when untrusted data is loaded.
    std::int64_t min = std::numeric_limits<std::int64_t>::min();
    std::int64_t max = std::numeric_limits<std::int64_t>::max();
};

struct FieldInfo {
    std::string_view name;
    FieldType type = FieldType::Float;
    std::uint32_t offset = 0;
    FieldOptions options;

    constexpr FieldInfo(std::string_view fieldName, FieldType fieldType, std::uint32_t fieldOffset)
        : name(fieldName), type(fieldType), offset(fieldOffset) {}

    // Options, chained after CT_FIELD.
    constexpr FieldInfo withHint(FieldHint hint) const {
        FieldInfo copy = *this;
        copy.options.hint = hint;
        return copy;
    }
    constexpr FieldInfo withDescription(std::string_view description) const {
        FieldInfo copy = *this;
        copy.options.description = description;
        return copy;
    }
    constexpr FieldInfo withFlagNames(std::span<const std::string_view> flagNames) const {
        FieldInfo copy = *this;
        copy.options.hint = FieldHint::Flags;
        copy.options.flagNames = flagNames;
        return copy;
    }
    constexpr FieldInfo withRange(std::int64_t min, std::int64_t max) const {
        FieldInfo copy = *this;
        copy.options.min = min;
        copy.options.max = max;
        return copy;
    }
};

struct TypeSchema {
    std::string_view name; // stable: saves and the network refer to types by name
    std::uint32_t size = 0;
    std::span<const FieldInfo> fields;
};

// Specialised for each described struct, as `static constexpr TypeSchema value`.
template <typename T>
struct Schema;

template <typename T>
struct FieldTypeOf;
template <>
struct FieldTypeOf<float> : std::integral_constant<FieldType, FieldType::Float> {};
template <>
struct FieldTypeOf<Vec3> : std::integral_constant<FieldType, FieldType::Vec3> {};
template <>
struct FieldTypeOf<std::uint8_t> : std::integral_constant<FieldType, FieldType::U8> {};
template <>
struct FieldTypeOf<std::uint16_t> : std::integral_constant<FieldType, FieldType::U16> {};
template <>
struct FieldTypeOf<std::uint32_t> : std::integral_constant<FieldType, FieldType::U32> {};
template <>
struct FieldTypeOf<std::int16_t> : std::integral_constant<FieldType, FieldType::I16> {};
template <>
struct FieldTypeOf<std::int32_t> : std::integral_constant<FieldType, FieldType::I32> {};

constexpr std::uint32_t fieldSize(FieldType type) noexcept {
    switch (type) {
    case FieldType::U8:
        return 1;
    case FieldType::U16:
    case FieldType::I16:
        return 2;
    case FieldType::Float:
    case FieldType::U32:
    case FieldType::I32:
        return 4;
    case FieldType::Entity:
        return 8;
    case FieldType::Vec3:
        return 12;
    }
    return 0;
}

std::string_view fieldTypeName(FieldType type) noexcept;

// True if the fields are in declaration order and together cover every byte of the struct: no
// field left out and no padding. Every schema is checked with this in a static_assert.
constexpr bool describesEveryByte(const TypeSchema& schema) noexcept {
    std::uint32_t expected = 0;
    for (const FieldInfo& field : schema.fields) {
        if (field.offset != expected) {
            return false;
        }
        expected += fieldSize(field.type);
    }
    return expected == schema.size;
}

// One field of a schema, with options chained after it:
//     CT_FIELD(Transform, yaw).withHint(ct::FieldHint::Angle).withDescription("Facing")
#define CT_FIELD(Type, member)                                                                     \
    ::ct::FieldInfo(#member, ::ct::FieldTypeOf<std::remove_cv_t<decltype(Type::member)>>::value,   \
                    static_cast<std::uint32_t>(offsetof(Type, member)))

// One element of a std::array member, named "member[index]". List every element, in order.
#define CT_FIELD_ELEMENT(Type, member, index)                                                      \
    ::ct::FieldInfo(                                                                               \
        #member "[" #index "]",                                                                    \
        ::ct::FieldTypeOf<std::remove_cv_t<typename decltype(Type::member)::value_type>>::value,   \
        static_cast<std::uint32_t>(offsetof(Type, member) +                                        \
                                   (index) * sizeof(typename decltype(Type::member)::value_type)))

// Checks a loaded value: floats must be finite, integers inside the field's range. Any entity
// reference is valid: it may name an entity that has since gone, which the generation shows.
bool isValidFieldValue(const FieldInfo& field, std::span<const std::byte> object);

// Values copied in and out of an object's bytes by field, so generic code never casts a struct.
float readFloat(std::span<const std::byte> object, std::uint32_t offset);
Vec3 readVec3(std::span<const std::byte> object, std::uint32_t offset);
std::int64_t readInteger(const FieldInfo& field, std::span<const std::byte> object);
void writeFloat(std::span<std::byte> object, std::uint32_t offset, float value);
void writeVec3(std::span<std::byte> object, std::uint32_t offset, Vec3 value);
void writeInteger(const FieldInfo& field, std::span<std::byte> object, std::int64_t value);

struct EntityRefValue {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;
};
EntityRefValue readEntityRef(std::span<const std::byte> object, std::uint32_t offset);
void writeEntityRef(std::span<std::byte> object, std::uint32_t offset, EntityRefValue value);

class ByteWriter;
class ByteReader;

// Binary encoding of one field's value: little-endian, fieldSize(type) bytes, floats as their bits.
void encodeField(ByteWriter& writer, const FieldInfo& field, std::span<const std::byte> object);
void decodeField(ByteReader& reader, const FieldInfo& field, std::span<std::byte> object);

} // namespace ct
