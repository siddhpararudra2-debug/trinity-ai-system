#include <doctest.h>

#include <memory>

#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/intelligence/Intent.hpp"
#include "trinity/intelligence/IntentRouter.hpp"
#include "trinity/intelligence/IntentValidator.hpp"
#include "trinity/intelligence/RequirementParser.hpp"
#include "trinity/validation/ValidationResult.hpp"

using trinity::engines::EngineRegistry;
using trinity::engines::registerAllEngines;
using trinity::intelligence::IntentRouter;
using trinity::intelligence::IntentValidator;
using trinity::intelligence::ParseStatus;
using trinity::intelligence::RequirementParser;

namespace {

struct IntentFixture {
    EngineRegistry registry;
    RequirementParser parser;
    IntentValidator validator;
    IntentRouter router;

    IntentFixture() { registerAllEngines(registry); }
};

}  // namespace

// Regression: vision intents previously failed isValidDomain and could
// never reach the router. Parse -> validate -> route must all accept.
TEST_CASE("vision intent parses, validates, and routes") {
    IntentFixture fx;
    const auto parsed = fx.parser.parse("Resize image photo.png to 100x100");
    const bool parsedOk = parsed.status == ParseStatus::Valid; REQUIRE(parsedOk);
    REQUIRE(parsed.intent.domain == "vision");
    CHECK(parsed.intent.operation == "resize_image");
    CHECK(parsed.intent.parameters["width"] == 100);
    CHECK(parsed.intent.parameters["height"] == 100);
    CHECK(parsed.intent.missing.empty());

    const auto validation = fx.validator.validate(parsed.intent, fx.registry);
    const bool validated = validation.status == trinity::validation::ValidationStatus::Validated; CHECK(validated);
    CHECK(validation.passed());

    const auto routed = fx.router.route(parsed.intent, fx.registry);
    CHECK(routed.routed);
    CHECK(routed.engine == "vision");
    CHECK(routed.operation == "resize_image");
    CHECK(routed.toolCall.engine == "vision");
    CHECK(routed.toolCall.parameters["width"] == 100);
}

TEST_CASE("vision edge-detect intent validates and routes") {
    IntentFixture fx;
    const auto parsed = fx.parser.parse("Detect edges in image scan.png using canny");
    const bool parsedOk = parsed.status == ParseStatus::Valid; REQUIRE(parsedOk);
    REQUIRE(parsed.intent.domain == "vision");
    CHECK(parsed.intent.operation == "edge_detect");

    const auto validation = fx.validator.validate(parsed.intent, fx.registry);
    CHECK(validation.passed());

    const auto routed = fx.router.route(parsed.intent, fx.registry);
    CHECK(routed.routed);
    CHECK(routed.engine == "vision");
    CHECK(routed.operation == "edge_detect");
}

TEST_CASE("research intent parses, validates, and routes") {
    IntentFixture fx;
    const auto parsed = fx.parser.parse("Search for \"thermal cycling\"");
    const bool parsedOk = parsed.status == ParseStatus::Valid; REQUIRE(parsedOk);
    REQUIRE(parsed.intent.domain == "research");
    CHECK(parsed.intent.operation == "search");
    CHECK(parsed.intent.parameters["query"] == "thermal cycling");

    const auto validation = fx.validator.validate(parsed.intent, fx.registry);
    CHECK(validation.passed());

    const auto routed = fx.router.route(parsed.intent, fx.registry);
    CHECK(routed.routed);
    CHECK(routed.engine == "research");
    CHECK(routed.operation == "search");
    CHECK(routed.toolCall.parameters["query"] == "thermal cycling");
}

TEST_CASE("research index intent carries title and text through the router") {
    IntentFixture fx;
    const auto parsed =
        fx.parser.parse("Index document \"Copper notes\" with text \"Copper wire solders easily\"");
    const bool parsedOk = parsed.status == ParseStatus::Valid; REQUIRE(parsedOk);
    REQUIRE(parsed.intent.domain == "research");
    CHECK(parsed.intent.operation == "index_document");
    CHECK(parsed.intent.parameters["title"] == "Copper notes");

    const auto validation = fx.validator.validate(parsed.intent, fx.registry);
    CHECK(validation.passed());

    const auto routed = fx.router.route(parsed.intent, fx.registry);
    CHECK(routed.routed);
    CHECK(routed.engine == "research");
    CHECK(routed.toolCall.parameters["title"] == "Copper notes");
    CHECK(routed.toolCall.parameters["text"] == "Copper wire solders easily");
}

TEST_CASE("research operations are validated against the engine capability list") {
    IntentFixture fx;
    trinity::intelligence::Intent intent;
    intent.intentId = "test-intent-web-search";
    intent.domain = "research";
    intent.object = "query";
    intent.operation = "web_search";
    intent.status = ParseStatus::Valid;
    const auto validation = fx.validator.validate(intent, fx.registry);
    const bool rejected = validation.status == trinity::validation::ValidationStatus::Invalid; CHECK(rejected);
}
