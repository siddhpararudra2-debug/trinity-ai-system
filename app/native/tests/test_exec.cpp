#include <doctest.h>

#include "trinity/cli/Exec.hpp"

// --exec protocol: pure request/response, verified without spawning.
// Mirrors the Python bridge contract (src/engines/native_bridge.py):
// one EngineRequest JSON in, one ToolResponse-shaped envelope out.

TEST_CASE("exec rejects invalid JSON with a success:false envelope") {
    const trinity::core::Json env =
        trinity::cli::executeRequestJson("not json{{");
    CHECK(env["success"] == false);
    CHECK(env["errors"].is_array());
    CHECK(env["errors"].size() == 1);
}

TEST_CASE("exec rejects non-object JSON") {
    const trinity::core::Json env = trinity::cli::executeRequestJson("[1, 2]");
    CHECK(env["success"] == false);
}

TEST_CASE("exec reports unknown engines truthfully") {
    const trinity::core::Json env = trinity::cli::executeRequestJson(
        R"({"engine":"ghost-engine-xyz","operation":"x","parameters":{}})");
    CHECK(env["success"] == false);
    CHECK(env["engine"] == "ghost-engine-xyz");
    CHECK(env["errors"][0]["code"] == "engine_not_found");
}

TEST_CASE("exec evaluates math end to end") {
    const trinity::core::Json env = trinity::cli::executeRequestJson(
        R"({"engine":"math","operation":"evaluate_expression","parameters":{"expression":"2 + 3 * 4"}})");
    CHECK(env["success"] == true);
    CHECK(env["result"]["value"] == 14.0);
}

TEST_CASE("exec reports unsupported operations truthfully") {
    const trinity::core::Json env = trinity::cli::executeRequestJson(
        R"({"engine":"cad","operation":"generate_quadcopter_frame","parameters":{}})");
    CHECK(env["success"] == false);
}
