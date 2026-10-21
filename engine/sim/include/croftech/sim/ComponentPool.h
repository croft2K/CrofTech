#pragma once

#include "croftech/core/Assert.h"
#include "croftech/sim/EntityId.h"

#include <array>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ct {

// Packed storage for one component type (a sparse set). Components sit contiguously in the order
// they were added; removal moves the last one into the gap. Fixed capacity and plain data, so a
// pool is copied with no allocation and iteration order is deterministic.
template <typename T, std::uint32_t Capacity>
class ComponentPool {
    static_assert(std::is_trivially_copyable_v<T>, "components must be plain data");

public:
    using Component = T;

    T& add(EntityId id, const T& value) noexcept {
        CT_ASSERT(id.index < Capacity);
        CT_ASSERT(!has(id));
        CT_ASSERT(m_count < Capacity);
        const std::uint32_t slot = m_count++;
        m_sparse[id.index] = slot;
        m_ids[slot] = id;
        m_components[slot] = value;
        return m_components[slot];
    }

    void remove(EntityId id) noexcept {
        if (!has(id)) {
            return;
        }
        const std::uint32_t slot = m_sparse[id.index];
        const std::uint32_t last = --m_count;
        m_components[slot] = m_components[last];
        m_ids[slot] = m_ids[last];
        m_sparse[m_ids[slot].index] = slot;
    }

    bool has(EntityId id) const noexcept {
        if (id.index >= Capacity) {
            return false;
        }
        const std::uint32_t slot = m_sparse[id.index];
        return slot < m_count && m_ids[slot] == id;
    }

    T* get(EntityId id) noexcept { return has(id) ? &m_components[m_sparse[id.index]] : nullptr; }
    const T* get(EntityId id) const noexcept {
        return has(id) ? &m_components[m_sparse[id.index]] : nullptr;
    }

    std::uint32_t size() const noexcept { return m_count; }
    std::span<T> components() noexcept { return {m_components.data(), m_count}; }
    std::span<const T> components() const noexcept { return {m_components.data(), m_count}; }
    std::span<const EntityId> entities() const noexcept { return {m_ids.data(), m_count}; }

private:
    std::array<T, Capacity> m_components{};
    std::array<EntityId, Capacity> m_ids{};
    std::array<std::uint32_t, Capacity> m_sparse{};
    std::uint32_t m_count = 0;
};

} // namespace ct
