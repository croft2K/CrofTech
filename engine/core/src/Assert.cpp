#include "croftech/core/Assert.h"

#include <cstdio>
#include <cstdlib>

namespace ct::detail {

void assertFailed(const char* expression, const char* file, int line) noexcept {
    std::fprintf(stderr, "%s(%d): assertion failed: %s\n", file, line, expression);
    std::fflush(stderr);
    std::abort();
}

} // namespace ct::detail
