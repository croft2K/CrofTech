#pragma once

// CT_ASSERT checks programmer errors only. It is compiled out of release builds, so never use it
// to validate untrusted data (packets, files): those paths must check and return an error.

namespace ct::detail {
[[noreturn]] void assertFailed(const char* expression, const char* file, int line) noexcept;
} // namespace ct::detail

#if defined(CT_ASSERTS_ENABLED)
#define CT_ASSERT(condition)                                                                       \
    ((condition) ? static_cast<void>(0)                                                            \
                 : ::ct::detail::assertFailed(#condition, __FILE__, __LINE__))
#else
#define CT_ASSERT(condition) static_cast<void>(0)
#endif
