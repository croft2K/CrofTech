#include "croftech/core/Hash.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>

namespace {

// Published FNV-1a 64-bit test vectors.
static_assert(ct::hashString("") == 0xcbf29ce484222325ull);
static_assert(ct::hashString("a") == 0xaf63dc4c8601ec8cull);
static_assert(ct::hashString("foobar") == 0x85944171f73967e8ull);

} // namespace

TEST_CASE("hashBytes and hashString agree on the same bytes") {
    const std::array bytes{std::byte{'f'}, std::byte{'o'}, std::byte{'o'},
                           std::byte{'b'}, std::byte{'a'}, std::byte{'r'}};
    CHECK(ct::hashBytes(bytes) == ct::hashString("foobar"));
}

TEST_CASE("chaining the seed hashes buffers as one stream") {
    const std::uint64_t whole = ct::hashString("foobar");
    const std::uint64_t chained = ct::hashString("bar", ct::hashString("foo"));
    CHECK(chained == whole);
}

TEST_CASE("one changed byte changes the hash") {
    CHECK(ct::hashString("tick 1000") != ct::hashString("tick 1001"));
}
