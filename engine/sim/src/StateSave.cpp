#include "croftech/sim/StateSave.h"

#include "croftech/core/ByteStream.h"
#include "croftech/core/Schema.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ct {
namespace {

constexpr std::uint32_t kMagic = 0x54535443; // "CTST" in file order
constexpr std::uint32_t kMaxPoolsInSave = 64;
constexpr std::uint32_t kMaxFieldsInSave = 64;
constexpr std::uint8_t kFieldTypeCount = static_cast<std::uint8_t>(FieldType::Entity) + 1;

template <typename Pool>
const TypeSchema& schemaOfPool() {
    return Schema<typename std::remove_cvref_t<Pool>::Component>::value;
}

// How one field listed in a save maps onto the current build's component.
struct SavedField {
    FieldType type = FieldType::Float;
    const FieldInfo* target = nullptr; // null: the build has no such field; skip its bytes
};

class StateLoader {
public:
    StateLoader(std::span<const std::byte> bytes, std::string& error)
        : m_reader(bytes), m_error(error) {}

    bool load(State& state);

private:
    bool fail(std::string message) {
        m_error = std::move(message);
        return false;
    }

    bool readEntities(World& world);
    bool readPool(World& world);
    bool readFields(const TypeSchema* schema, std::vector<SavedField>& fields);
    template <typename Pool>
    bool readComponents(const World& world, Pool& pool, const std::vector<SavedField>& fields,
                        std::uint32_t count);
    bool skipComponents(const std::vector<SavedField>& fields, std::uint32_t count);

    ByteReader m_reader;
    std::string& m_error;
    std::vector<std::string> m_seenPools;
};

bool StateLoader::load(State& state) {
    if (m_reader.readU32() != kMagic) {
        return fail("not a CrofTech state save");
    }
    if (const std::uint32_t version = m_reader.readU32(); version != kStateSaveVersion) {
        return fail(std::format("unsupported save version {}", version));
    }
    auto loaded = std::make_unique<State>();
    loaded->tick = m_reader.readU32();
    if (!readEntities(loaded->world)) {
        return false;
    }
    const std::uint32_t poolCount = m_reader.readU32();
    if (!m_reader.ok() || poolCount > kMaxPoolsInSave) {
        return fail("bad component type count");
    }
    for (std::uint32_t i = 0; i < poolCount; ++i) {
        if (!readPool(loaded->world)) {
            return false;
        }
    }
    if (!m_reader.ok() || !m_reader.atEnd()) {
        return fail("save is truncated or has trailing bytes");
    }
    state = *loaded;
    return true;
}

bool StateLoader::readEntities(World& world) {
    const std::uint32_t highWater = m_reader.readU32();
    if (!m_reader.ok() || highWater > kMaxEntities) {
        return fail("bad entity count");
    }
    std::vector<std::uint32_t> generations(highWater);
    for (std::uint32_t& generation : generations) {
        generation = m_reader.readU32();
    }
    std::vector<std::uint8_t> alive(highWater);
    for (std::uint8_t& flag : alive) {
        flag = m_reader.readU8();
    }
    const std::uint32_t freeCount = m_reader.readU32();
    if (!m_reader.ok() || freeCount > highWater) {
        return fail("bad free entity count");
    }
    std::vector<std::uint32_t> freeList(freeCount);
    for (std::uint32_t& index : freeList) {
        index = m_reader.readU32();
    }
    if (!m_reader.ok() || !world.entities.restore(generations, alive, freeList)) {
        return fail("entity table is inconsistent");
    }
    return true;
}

bool StateLoader::readPool(World& world) {
    const std::string name(m_reader.readShortString());
    if (!m_reader.ok()) {
        return fail("save is truncated");
    }
    for (const std::string& seen : m_seenPools) {
        if (seen == name) {
            return fail(std::format("component type '{}' appears twice", name));
        }
    }
    m_seenPools.push_back(name);

    const TypeSchema* schema = nullptr;
    forEachPool(world, [&](auto& pool) {
        if (schemaOfPool<decltype(pool)>().name == name) {
            schema = &schemaOfPool<decltype(pool)>();
        }
    });
    std::vector<SavedField> fields;
    if (!readFields(schema, fields)) {
        return false;
    }
    const std::uint32_t count = m_reader.readU32();
    if (!m_reader.ok() || count > kMaxEntities) {
        return fail(std::format("bad component count for '{}'", name));
    }
    if (schema == nullptr) {
        return skipComponents(fields, count);
    }
    bool ok = true;
    forEachPool(world, [&](auto& pool) {
        if (&schemaOfPool<decltype(pool)>() == schema) {
            ok = readComponents(world, pool, fields, count);
        }
    });
    return ok;
}

bool StateLoader::readFields(const TypeSchema* schema, std::vector<SavedField>& fields) {
    const std::uint8_t fieldCount = m_reader.readU8();
    if (!m_reader.ok() || fieldCount > kMaxFieldsInSave) {
        return fail("bad field count");
    }
    std::vector<std::string_view> names;
    for (std::uint8_t i = 0; i < fieldCount; ++i) {
        const std::string_view name = m_reader.readShortString();
        const std::uint8_t typeCode = m_reader.readU8();
        if (!m_reader.ok() || typeCode >= kFieldTypeCount) {
            return fail("bad field description");
        }
        for (const std::string_view seen : names) {
            if (seen == name) {
                return fail("a field appears twice");
            }
        }
        names.push_back(name);

        SavedField field{static_cast<FieldType>(typeCode), nullptr};
        if (schema != nullptr) {
            for (const FieldInfo& info : schema->fields) {
                if (info.name != name) {
                    continue;
                }
                if (info.type != field.type) {
                    return fail(std::format("field {}.{} is saved as {} but is now {}",
                                            schema->name, info.name, fieldTypeName(field.type),
                                            fieldTypeName(info.type)));
                }
                field.target = &info;
            }
        }
        fields.push_back(field);
    }
    return true;
}

template <typename Pool>
bool StateLoader::readComponents(const World& world, Pool& pool,
                                 const std::vector<SavedField>& fields, std::uint32_t count) {
    using T = typename Pool::Component;
    const TypeSchema& schema = schemaOfPool<Pool>();
    for (std::uint32_t i = 0; i < count; ++i) {
        const EntityId id{m_reader.readU32(), m_reader.readU32()};
        if (!m_reader.ok()) {
            return fail("save is truncated");
        }
        if (!world.entities.isAlive(id)) {
            return fail(std::format("{} belongs to an entity that does not exist", schema.name));
        }
        if (pool.has(id)) {
            return fail(std::format("entity {} has two {} components", id.index, schema.name));
        }
        T component{};
        const std::span<std::byte> bytes = std::as_writable_bytes(std::span{&component, 1});
        for (const SavedField& field : fields) {
            if (field.target == nullptr) {
                m_reader.readBytes(fieldSize(field.type));
                continue;
            }
            decodeField(m_reader, *field.target, bytes);
            if (m_reader.ok() && !isValidFieldValue(*field.target, bytes)) {
                return fail(std::format("{}.{} of entity {} is out of range", schema.name,
                                        field.target->name, id.index));
            }
        }
        if (!m_reader.ok()) {
            return fail("save is truncated");
        }
        pool.add(id, component);
    }
    return true;
}

bool StateLoader::skipComponents(const std::vector<SavedField>& fields, std::uint32_t count) {
    std::size_t componentBytes = 8; // entity id
    for (const SavedField& field : fields) {
        componentBytes += fieldSize(field.type);
    }
    m_reader.readBytes(componentBytes * count);
    return m_reader.ok() || fail("save is truncated");
}

} // namespace

std::vector<std::byte> saveState(const State& state) {
    ByteWriter writer;
    writeState(writer, state);
    return writer.take();
}

void writeState(ByteWriter& writer, const State& state) {
    writer.writeU32(kMagic);
    writer.writeU32(kStateSaveVersion);
    writer.writeU32(state.tick);

    const auto& entities = state.world.entities;
    writer.writeU32(entities.highWater());
    for (const std::uint32_t generation : entities.generations()) {
        writer.writeU32(generation);
    }
    for (const std::uint8_t alive : entities.aliveFlags()) {
        writer.writeU8(alive);
    }
    writer.writeU32(static_cast<std::uint32_t>(entities.freeList().size()));
    for (const std::uint32_t index : entities.freeList()) {
        writer.writeU32(index);
    }

    std::uint32_t poolCount = 0;
    forEachPool(state.world, [&poolCount](const auto&) { ++poolCount; });
    writer.writeU32(poolCount);
    forEachPool(state.world, [&writer](const auto& pool) {
        const TypeSchema& schema = schemaOfPool<decltype(pool)>();
        writer.writeShortString(schema.name);
        writer.writeU8(static_cast<std::uint8_t>(schema.fields.size()));
        for (const FieldInfo& field : schema.fields) {
            writer.writeShortString(field.name);
            writer.writeU8(static_cast<std::uint8_t>(field.type));
        }
        writer.writeU32(pool.size());
        for (std::uint32_t i = 0; i < pool.size(); ++i) {
            writer.writeU32(pool.entities()[i].index);
            writer.writeU32(pool.entities()[i].generation);
            const auto bytes = std::as_bytes(std::span{&pool.components()[i], 1});
            for (const FieldInfo& field : schema.fields) {
                encodeField(writer, field, bytes);
            }
        }
    });
}

bool loadState(std::span<const std::byte> bytes, State& state, std::string& error) {
    return StateLoader(bytes, error).load(state);
}

} // namespace ct
