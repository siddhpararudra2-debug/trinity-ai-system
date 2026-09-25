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

TEST_CASE("all eight engines register metadata and describe real capabilities") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    const auto listed = registry.list();
    CHECK(listed.size() == 8);

    // Every shipped domain is a real engine now: no stubbed domains remain.
    // Math does not expose `describe`; it is covered by listCapabilities below.
    for (const char* name :
         {"cad", "pcb", "firmware", "simulation", "vision", "research",
          "robotics"}) {
        CHECK(registry.has(name));
        trinity::engines::EngineRequest req;
        req.engine = name;
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        REQUIRE(result.errors.empty());
    }

    // Research graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("research"));
        trinity::engines::EngineRequest req;
        req.engine = "research";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        REQUIRE(result.result.contains("capabilities"));
        CHECK(result.result["capabilities"].size() == 7);
    }

    // Robotics graduated to a real engine: describe succeeds.
    {
        CHECK(registry.has("robotics"));
        trinity::engines::EngineRequest req;
        req.engine = "robotics";
        req.operation = "describe";
        const auto result = registry.execute(req);
        CHECK(result.success);
        REQUIRE(result.result.contains("capabilities"));
        CHECK(result.result["capabilities"].size() == 12);
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
