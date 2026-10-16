#include <doctest.h>

#include <string>
#include <vector>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/Parts.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"

// Ports tests/test_cad_parts.py: registry dispatch, unknown types,
// defaults, and per-part validated geometry for the box-only parts.

TEST_CASE("part registry lists the supported parts sorted") {
    const std::vector<std::string> names = trinity::cad::partNames();
    REQUIRE(names.size() == 5);
    CHECK(names[0] == "enclosure");
    CHECK(names[1] == "l_bracket");
    CHECK(names[2] == "mounting_plate");
    CHECK(names[3] == "quadcopter_frame");
    CHECK(names[4] == "standoff");
}

TEST_CASE("findPart returns null for unknown types") {
    CHECK(trinity::cad::findPart("nope") == nullptr);
    CHECK(trinity::cad::findPart("") == nullptr);
}

TEST_CASE("quadcopter adapter matches the bespoke path") {
    const trinity::cad::PartDefinition* part =
        trinity::cad::findPart("quadcopter_frame");
    REQUIRE(part != nullptr);
    const trinity::cad::PartSpec spec =
        part->fromRequest(trinity::core::Json::object());
    CHECK(spec.parameters.at("overall_size") == 50.0);
    const trinity::cad::Mesh viaRegistry = part->builder(spec);
    const trinity::cad::FrameParams direct =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    const trinity::cad::Mesh viaDirect = trinity::cad::buildQuadcopterFrame(direct);
    CHECK(viaRegistry.triangleCount() == viaDirect.triangleCount());
    CHECK(viaRegistry.triangleCount() == 108);
    const trinity::cad::FrameValidation validation = part->validator(spec, viaRegistry);
    CHECK(validation.ok);
}

TEST_CASE("enclosure defaults generate validated 60-triangle geometry") {
    const trinity::cad::PartDefinition* part = trinity::cad::findPart("enclosure");
    REQUIRE(part != nullptr);
    const trinity::cad::PartSpec spec =
        part->fromRequest(trinity::core::Json::object());
    CHECK(spec.parameters.at("length") == 100.0);
    const trinity::cad::Mesh mesh = part->builder(spec);
    CHECK(mesh.triangleCount() == 60);
    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    CHECK(bbox.min[0] == doctest::Approx(-50.0));
    CHECK(bbox.min[1] == doctest::Approx(-30.0));
    CHECK(bbox.min[2] == doctest::Approx(0.0));
    CHECK(bbox.max[0] == doctest::Approx(50.0));
    CHECK(bbox.max[1] == doctest::Approx(30.0));
    CHECK(bbox.max[2] == doctest::Approx(40.0));
    CHECK(part->validator(spec, mesh).ok);
    CHECK(part->filenameStem(spec) == "enclosure_100x60x40mm");
}

TEST_CASE("l_bracket defaults generate validated 24-triangle geometry") {
    const trinity::cad::PartDefinition* part = trinity::cad::findPart("l_bracket");
    REQUIRE(part != nullptr);
    const trinity::cad::Mesh mesh =
        part->builder(part->fromRequest(trinity::core::Json::object()));
    CHECK(mesh.triangleCount() == 24);
    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    CHECK(bbox.min[0] == doctest::Approx(-20.0));
    CHECK(bbox.min[1] == doctest::Approx(-10.0));
    CHECK(bbox.min[2] == doctest::Approx(0.0));
    CHECK(bbox.max[0] == doctest::Approx(20.0));
    CHECK(bbox.max[1] == doctest::Approx(10.0));
    CHECK(bbox.max[2] == doctest::Approx(40.0));
    CHECK(part->validator(part->fromRequest(trinity::core::Json::object()), mesh).ok);
    CHECK(part->filenameStem(part->fromRequest(trinity::core::Json::object())) ==
          "l_bracket_40x40x20mm");
}

TEST_CASE("parts reject unknown parameters") {
    const trinity::cad::PartDefinition* part = trinity::cad::findPart("enclosure");
    REQUIRE(part != nullptr);
    CHECK_THROWS_AS(part->fromRequest(trinity::core::Json{{"nope", 1.0}}),
                    trinity::core::RequestValidationError);
}

TEST_CASE("parts reject out-of-range parameters") {
    const trinity::cad::PartDefinition* part = trinity::cad::findPart("enclosure");
    REQUIRE(part != nullptr);
    CHECK_THROWS_AS(
        part->fromRequest(trinity::core::Json{{"length", 2000.0}}),
        trinity::core::RequestValidationError);
    const trinity::cad::PartDefinition* bracket = trinity::cad::findPart("l_bracket");
    REQUIRE(bracket != nullptr);
    CHECK_THROWS_AS(
        bracket->fromRequest(trinity::core::Json{{"thickness", 0.5}}),
        trinity::core::RequestValidationError);
}

TEST_CASE("parts reject cross-field violations") {
    const trinity::cad::PartDefinition* enclosure =
        trinity::cad::findPart("enclosure");
    REQUIRE(enclosure != nullptr);
    CHECK_THROWS_AS(
        enclosure->fromRequest(
            trinity::core::Json{{"length", 10.0}, {"width", 10.0}, {"wall_thickness", 6.0}}),
        trinity::core::RequestValidationError);
    const trinity::cad::PartDefinition* bracket = trinity::cad::findPart("l_bracket");
    REQUIRE(bracket != nullptr);
    CHECK_THROWS_AS(
        bracket->fromRequest(trinity::core::Json{{"thickness", 40.0}}),
        trinity::core::RequestValidationError);
}

TEST_CASE("cad engine generates a registered second part end to end") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    trinity::engines::EngineRequest request;
    request.engine = "cad";
    request.operation = "generate";
    request.parameters = trinity::core::Json{
        {"type", "enclosure"}, {"parameters", {{"length", 100.0}}}, {"outputs", {"json"}}};
    const trinity::engines::EngineResult result = registry.execute(request);
    CHECK(result.success);
    CHECK(result.result["triangle_count"] == 60);
    CHECK(result.result["type"] == "enclosure");
}

TEST_CASE("cad engine rejects unknown types with the supported list") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    trinity::engines::EngineRequest request;
    request.engine = "cad";
    request.operation = "generate";
    request.parameters = trinity::core::Json{{"type", "nope"}};
    CHECK_THROWS_AS(registry.execute(request), trinity::core::RequestValidationError);
}
