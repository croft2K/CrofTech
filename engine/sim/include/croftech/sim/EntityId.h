#pragma once

#include "croftech/core/Schema.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ct {

// A handle to an entity. The generation changes each time an index is reused, so a handle to a
// destroyed entity never resolves to whatever reused its slot.
struct EntityId {
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    constexpr bool isValid() const noexcept { return index != kInvalidIndex; }
    friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
};
static_assert(sizeof(EntityId) == 8, "EntityId is stored as FieldType::Entity");

template <>
struct FieldTypeOf<EntityId> : std::integral_constant<FieldType, FieldType::Entity> {};

// Fixed-capacity entity allocator. Plain data: copying it copies the allocation state exactly.
// Freed indices are reused last-in first-out, so allocation order is deterministic.
template <std::uint32_t Capacity>
class EntityAllocator {
public:
    EntityId create() noexcept {
        std::uint32_t index = 0;
        if (m_freeCount > 0) {
            index = m_freeList[--m_freeCount];
        } else if (m_highWater < Capacity) {
            index = m_highWater++;
        } else {
            return {};
        }
        m_alive[index] = 1;
        return {index, m_generations[index]};
    }

    bool destroy(EntityId id) noexcept {
        if (!isAlive(id)) {
            return false;
        }
        m_alive[id.index] = 0;
        ++m_generations[id.index];
        m_freeList[m_freeCount++] = id.index;
        return true;
    }

    // Replaces the allocation state with saved data. Fails and changes nothing unless the data is
    // consistent: equal-length generation and alive arrays within capacity, alive flags of 0 or 1,
    // and every index that is not alive in the free list exactly once.
    bool restore(std::span<const std::uint32_t> generations, std::span<const std::uint8_t> alive,
                 std::span<const std::uint32_t> freeList) noexcept {
        if (generations.size() != alive.size() || generations.size() > Capacity ||
            freeList.size() > generations.size()) {
            return false;
        }
        std::array<std::uint8_t, Capacity> listed{};
        for (const std::uint32_t index : freeList) {
            if (index >= alive.size() || alive[index] != 0 || listed[index] != 0) {
                return false;
            }
            listed[index] = 1;
        }
        for (std::size_t i = 0; i < alive.size(); ++i) {
            if (alive[i] > 1 || (alive[i] == 0) != (listed[i] != 0)) {
                return false;
            }
        }
        *this = {};
        std::copy(generations.begin(), generations.end(), m_generations.begin());
        std::copy(alive.begin(), alive.end(), m_alive.begin());
        std::copy(freeList.begin(), freeList.end(), m_freeList.begin());
        m_highWater = static_cast<std::uint32_t>(generations.size());
        m_freeCount = static_cast<std::uint32_t>(freeList.size());
        return true;
    }

    bool isAlive(EntityId id) const noexcept {
        return id.index < m_highWater && m_alive[id.index] != 0 &&
               m_generations[id.index] == id.generation;
    }

    std::uint32_t highWater() const noexcept { return m_highWater; }
    std::span<const std::uint32_t> generations() const noexcept {
        return {m_generations.data(), m_highWater};
    }
    std::span<const std::uint8_t> aliveFlags() const noexcept {
        return {m_alive.data(), m_highWater};
    }
    std::span<const std::uint32_t> freeList() const noexcept {
        return {m_freeList.data(), m_freeCount};
    }

private:
    std::array<std::uint32_t, Capacity> m_generations{};
    std::array<std::uint32_t, Capacity> m_freeList{};
    std::array<std::uint8_t, Capacity> m_alive{};
    std::uint32_t m_highWater = 0;
    std::uint32_t m_freeCount = 0;
};

} // namespace ct
