#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ct {

// The memory a script call runs in: one block, at the start of a 4 GB-aligned stretch of address
// space, from which every allocation of the call comes. Luau hashes pointers by their low 32 bits,
// so with the block there a pointer's hash is its offset in the block. The same calls then make the
// same allocations at the same offsets in every process and on every platform, and tables keyed
// by tables or functions iterate in the same order everywhere.
//
// Freed blocks are kept on a list for their size class and reused, last freed first, so memory a
// call lets go of serves it again; reset() forgets everything at once for the next call.
class ScriptArena {
public:
    // Reserves and commits size bytes (a multiple of 64 KB). If the address space can't be had,
    // valid() is false and every allocation fails.
    explicit ScriptArena(std::size_t size);
    ~ScriptArena();
    ScriptArena(const ScriptArena&) = delete;
    ScriptArena& operator=(const ScriptArena&) = delete;

    bool valid() const { return m_base != nullptr; }
    void reset();

    // Null when the arena is full. Blocks are 16-byte aligned.
    void* allocate(std::size_t size);
    // The size must be the one the block was allocated with.
    void release(void* block, std::size_t size);
    // A block of the new size with the old one's bytes (as many as fit): the same block if both
    // sizes are of one class. Null, leaving the block alone, when the arena is full.
    void* resize(void* block, std::size_t oldSize, std::size_t newSize);
    // How far allocation has reached, for tests and tuning.
    std::size_t used() const { return m_top; }
    // Where a block is in the arena, which unlike its address is the same in every process.
    std::uintptr_t offsetOf(const void* block) const {
        return reinterpret_cast<std::uintptr_t>(block) - reinterpret_cast<std::uintptr_t>(m_base);
    }

private:
    // Sizes up to kSmallLimit go in classes 16 bytes apart; larger ones in powers of two.
    static constexpr std::size_t kGranule = 16;
    static constexpr std::size_t kSmallLimit = 1024;
    static constexpr std::size_t kClassCount = kSmallLimit / kGranule + 64;

    static std::size_t sizeClass(std::size_t size);
    static std::size_t classSize(std::size_t sizeClass);

    std::byte* m_base = nullptr;
    std::size_t m_size = 0;
    std::size_t m_top = 0;
    std::array<void*, kClassCount> m_free{};
};

} // namespace ct
