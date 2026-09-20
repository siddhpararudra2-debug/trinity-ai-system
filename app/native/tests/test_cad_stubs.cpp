#include <doctest.h>

#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"

TEST_CASE("cad skeleton describes capabilities and refuses geometry") {
    trinity::engines::CadEngine cad;
    CHECK(cad.name() == "cad");

    trinity::engines::EngineRequest describe;
    describe.engine = "cad";
    describe.operation = "describe";
    const auto ok = cad.execute(describe);
    CHECK(ok.success);

    trinity::engines::EngineRequest geo;
    geo.engine = "cad";
    geo.operation = "generate_quadcopter_frame";
    const auto refused = cad.execute(geo);
    CHECK_FALSE(refused.success);
    REQUIRE_FALSE(refused.errors.empty());
    CHECK(refused.errors.front().value("code", "") == "capability_unavailable");
    REQUIRE(refused.validation.has_value());
    CHECK_FALSE(refused.validation->passed());
}

TEST_CASE("domain stubs register metadata and refuse execution") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    const auto listed = registry.list();
    CHECK(listed.size() == 8);

    for (const char* name : {"pcb", "firmware", "vision", "research", "simulation",
                             "robotics"}) {
        CHECK(registry.has(name));
        trinity::engines::EngineRequest req;
        req.engine = name;
        req.operation = "describe";
        const auto result = registry.execute(req);
        // Stubs never claim real work: success must stay false.
        CHECK_FALSE(result.success);
        REQUIRE_FALSE(result.errors.empty());
        CHECK(result.errors.front().value("code", "") == "capability_unavailable");
    }
}

TEST_CASE("registry exposes all eight engines with capabilities") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    CHECK(registry.has("math"));
    CHECK(registry.has("cad"));
    CHECK_FALSE(registry.listCapabilities("math").empty());
    CHECK_FALSE(registry.listCapabilities("cad").empty());
}
