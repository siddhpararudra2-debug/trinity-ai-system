#include <doctest.h>

#include "trinity/engines/CadEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/PcbEngine.hpp"
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

    for (const char* name : {"research", "robotics"}) {
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

    // Vision graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("vision"));
        trinity::engines::EngineRequest req;
        req.engine = "vision";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        const bool hasFormats = result.result.contains("supported_formats");
        const bool hasCaps = result.result.contains("capabilities");
        const bool hasDescribePayload = hasFormats || hasCaps;
        CHECK(hasDescribePayload);
    }

    // Simulation graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("simulation"));
        trinity::engines::EngineRequest req;
        req.engine = "simulation";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        CHECK(result.result.contains("supported_types"));
    }

    // Firmware graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("firmware"));
        trinity::engines::EngineRequest req;
        req.engine = "firmware";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        CHECK(result.result.contains("mcus"));
    }

    // The PCB domain graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("pcb"));
        trinity::engines::EngineRequest req;
        req.engine = "pcb";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        CHECK(result.result.contains("footprints"));
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
