#include "ScriptArena.h"

#include <bit>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace ct {
namespace {

static_assert(sizeof(void*) == 8, "scripts need 64-bit address space");

constexpr std::uintptr_t kAlignment = std::uintptr_t{1} << 32;

std::uintptr_t alignUp(std::uintptr_t address) {
    return (address + kAlignment - 1) & ~(kAlignment - 1);
}

#if defined(_WIN32)

// Windows can't free part of a reservation, so this finds aligned space by reserving more than
// enough, lets it go and takes the aligned part. Another thread may take the space in between;
// then it tries again.
std::byte* reserveAligned(std::size_t size) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        void* probe = VirtualAlloc(nullptr, kAlignment + size, MEM_RESERVE, PAGE_NOACCESS);
        if (probe == nullptr) {
            return nullptr;
        }
        const std::uintptr_t aligned = alignUp(reinterpret_cast<std::uintptr_t>(probe));
        VirtualFree(probe, 0, MEM_RELEASE);
        void* block = VirtualAlloc(reinterpret_cast<void*>(aligned), size, MEM_RESERVE | MEM_COMMIT,
                                   PAGE_READWRITE);
        if (block != nullptr) {
            return static_cast<std::byte*>(block);
        }
    }
    return nullptr;
}

void releaseAligned(std::byte* base, std::size_t) { VirtualFree(base, 0, MEM_RELEASE); }

#else

std::byte* reserveAligned(std::size_t size) {
    void* probe = mmap(nullptr, kAlignment + size, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (probe == MAP_FAILED) {
        return nullptr;
    }
    const auto start = reinterpret_cast<std::uintptr_t>(probe);
    const std::uintptr_t aligned = alignUp(start);
    const std::uintptr_t end = start + kAlignment + size;
    if (aligned > start) {
        munmap(probe, aligned - start);
    }
    if (end > aligned + size) {
        munmap(reinterpret_cast<void*>(aligned + size), end - (aligned + size));
    }
    if (mprotect(reinterpret_cast<void*>(aligned), size, PROT_READ | PROT_WRITE) != 0) {
        munmap(reinterpret_cast<void*>(aligned), size);
        return nullptr;
    }
    return reinterpret_cast<std::byte*>(aligned);
}

void releaseAligned(std::byte* base, std::size_t size) { munmap(base, size); }

#endif

} // namespace

ScriptArena::ScriptArena(std::size_t size) : m_base(reserveAligned(size)), m_size(size) {
    if (m_base == nullptr) {
        m_size = 0;
    }
}

ScriptArena::~ScriptArena() {
    if (m_base != nullptr) {
        releaseAligned(m_base, m_size);
    }
}

void ScriptArena::reset() {
    m_top = 0;
    m_free.fill(nullptr);
}

std::size_t ScriptArena::sizeClass(std::size_t size) {
    if (size <= kSmallLimit) {
        return (size + kGranule - 1) / kGranule;
    }
    // The power of two at or above the size: 2048 for 1025 to 2048, and so on.
    return kSmallLimit / kGranule + static_cast<std::size_t>(std::bit_width(size - 1)) - 10;
}

std::size_t ScriptArena::classSize(std::size_t sizeClass) {
    if (sizeClass <= kSmallLimit / kGranule) {
        return sizeClass * kGranule;
    }
    return std::size_t{1} << (sizeClass - kSmallLimit / kGranule + 10);
}

void* ScriptArena::allocate(std::size_t size) {
    if (size == 0 || m_base == nullptr || size > m_size) {
        return nullptr;
    }
    const std::size_t found = sizeClass(size);
    if (void* block = m_free[found]) {
        std::memcpy(&m_free[found], block, sizeof(void*));
        return block;
    }
    const std::size_t bytes = classSize(found);
    if (bytes > m_size - m_top) {
        return nullptr;
    }
    void* block = m_base + m_top;
    m_top += bytes;
    return block;
}

void* ScriptArena::resize(void* block, std::size_t oldSize, std::size_t newSize) {
    if (block != nullptr && newSize <= m_size && sizeClass(oldSize) == sizeClass(newSize)) {
        return block;
    }
    void* moved = allocate(newSize);
    if (moved != nullptr && block != nullptr) {
        std::memcpy(moved, block, oldSize < newSize ? oldSize : newSize);
        release(block, oldSize);
    }
    return moved;
}

void ScriptArena::release(void* block, std::size_t size) {
    if (block == nullptr || size == 0) {
        return;
    }
    const std::size_t found = sizeClass(size);
    std::memcpy(block, &m_free[found], sizeof(void*));
    m_free[found] = block;
}

} // namespace ct
