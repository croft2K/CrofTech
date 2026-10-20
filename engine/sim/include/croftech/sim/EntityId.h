#pragma once

#include <array>
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
