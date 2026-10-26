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

TEST_CASE("daemon responds to ping and executes multiple requests") {
    std::string input =
        "{\"ping\":true}\n"
        "{\"engine\":\"math\",\"operation\":\"evaluate_expression\",\"parameters\":{\"expression\":\"5 * 5\"}}\n"
        "not-valid-json\n"
        "{\"engine\":\"math\",\"operation\":\"evaluate_expression\",\"parameters\":{\"expression\":\"100 / 4\"}}\n"
        "quit\n";
    std::istringstream in(input);
    std::ostringstream out;

    const int exitCode = trinity::cli::runDaemon(in, out);
    CHECK(exitCode == 0);

    std::istringstream outLines(out.str());
    std::string line1, line2, line3, line4;
    CHECK(std::getline(outLines, line1));
    CHECK(std::getline(outLines, line2));
    CHECK(std::getline(outLines, line3));
    CHECK(std::getline(outLines, line4));

    // Line 1: ping -> pong
    auto j1 = trinity::core::Json::parse(line1);
    CHECK(j1["pong"] == true);
    CHECK(j1["success"] == true);

    // Line 2: 5 * 5 = 25
    auto j2 = trinity::core::Json::parse(line2);
    CHECK(j2["success"] == true);
    CHECK(j2["result"]["value"] == 25.0);

    // Line 3: invalid JSON error envelope (daemon stays alive)
    auto j3 = trinity::core::Json::parse(line3);
    CHECK(j3["success"] == false);
    CHECK(j3["errors"].size() == 1);

    // Line 4: 100 / 4 = 25
    auto j4 = trinity::core::Json::parse(line4);
    CHECK(j4["success"] == true);
    CHECK(j4["result"]["value"] == 25.0);
}
