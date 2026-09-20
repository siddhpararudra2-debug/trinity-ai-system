#include <doctest.h>

#include <unordered_set>

#include "trinity/core/Uuid.hpp"

TEST_CASE("uuid generates unique v4 values") {
    std::unordered_set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        const std::string id = trinity::core::newUuid();
        CHECK(trinity::core::isValidUuid(id));
        CHECK(id.size() == 36);
        CHECK(id[8] == '-');
        CHECK(id[13] == '-');
        CHECK(id[18] == '-');
        CHECK(id[23] == '-');
        CHECK(seen.insert(id).second);
    }
}

TEST_CASE("uuid validation rejects malformed values") {
    CHECK_FALSE(trinity::core::isValidUuid(""));
    CHECK_FALSE(trinity::core::isValidUuid("not-a-uuid"));
    CHECK_FALSE(trinity::core::isValidUuid("00000000-0000-0000-0000-00000000000"));
    CHECK_FALSE(trinity::core::isValidUuid("00000000-0000-0000-0000-0000000000000"));
    CHECK_FALSE(trinity::core::isValidUuid("zzzzzzzz-0000-4000-8000-000000000000"));
    // Valid v4 sample.
    CHECK(trinity::core::isValidUuid("550e8400-e29b-41d4-a716-446655440000"));
}
