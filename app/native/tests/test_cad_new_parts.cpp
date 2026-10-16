#include <doctest.h>

#include <string>

#include "trinity/cad/Parts.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"

// Ports tests/test_cad_new_parts.py: defaults, stems, filename rules,
// unknown/out-of-range parameters, bound acceptance and cross-field
// violations for the cylinder-based parts.

namespace {

const trinity::cad::PartDefinition* requirePart(const std::string& name) {
    const trinity::cad::PartDefinition* part = trinity::cad::findPart(name);
    REQUIRE(part != nullptr);
    return part;
}

trinity::cad::PartSpec defaults(const trinity::cad::PartDefinition* part) {
    return part->fromRequest(trinity::core::Json::object());
}

}  // namespace

TEST_CASE("mounting_plate defaults generate validated 524-triangle geometry") {
    const trinity::cad::PartDefinition* part = requirePart("mounting_plate");
    const trinity::cad::PartSpec spec = defaults(part);
    CHECK(spec.parameters.at("length") == 100.0);
    CHECK(spec.parameters.at("boss_diameter") == 8.0);
    const trinity::cad::Mesh mesh = part->builder(spec);
    CHECK(mesh.triangleCount() == 524);
    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    CHECK(bbox.min[0] == doctest::Approx(-50.0));
    CHECK(bbox.min[1] == doctest::Approx(-30.0));
    CHECK(bbox.min[2] == doctest::Approx(0.0));
    CHECK(bbox.max[0] == doctest::Approx(50.0));
    CHECK(bbox.max[1] == doctest::Approx(30.0));
    CHECK(bbox.max[2] == doctest::Approx(5.0));
    CHECK(part->validator(spec, mesh).ok);
    CHECK(part->filenameStem(spec) == "mounting_plate_100x60mm");
}

TEST_CASE("standoff defaults generate validated 256-triangle geometry") {
    const trinity::cad::PartDefinition* part = requirePart("standoff");
    const trinity::cad::PartSpec spec = defaults(part);
    CHECK(spec.parameters.at("height") == 10.0);
    const trinity::cad::Mesh mesh = part->builder(spec);
    CHECK(mesh.triangleCount() == 256);
    const trinity::cad::BoundingBox bbox = mesh.boundingBox();
    CHECK(bbox.min[0] == doctest::Approx(-5.0));
    CHECK(bbox.min[1] == doctest::Approx(-5.0));
    CHECK(bbox.min[2] == doctest::Approx(0.0));
    CHECK(bbox.max[0] == doctest::Approx(5.0));
    CHECK(bbox.max[1] == doctest::Approx(5.0));
    CHECK(bbox.max[2] == doctest::Approx(10.0));
    CHECK(part->validator(spec, mesh).ok);
    CHECK(part->filenameStem(spec) == "standoff_10mm");
}

TEST_CASE("fractional dimensions produce dot-free filename stems") {
    const trinity::cad::PartDefinition* part = requirePart("mounting_plate");
    const trinity::cad::PartSpec spec = part->fromRequest(
        trinity::core::Json{{"length", 100.5}, {"width", 60.25}});
    const std::string stem = part->filenameStem(spec);
    CHECK(stem.find('.') == std::string::npos);
    CHECK(stem == "mounting_plate_100p5x60p25mm");
}

TEST_CASE("new parts reject unknown parameters") {
    CHECK_THROWS_AS(
        requirePart("mounting_plate")
            ->fromRequest(trinity::core::Json{{"nope", 1.0}}),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        requirePart("standoff")->fromRequest(trinity::core::Json{{"nope", 1.0}}),
        trinity::core::RequestValidationError);
}

TEST_CASE("new parts reject out-of-range parameters") {
    CHECK_THROWS_AS(
        requirePart("mounting_plate")
            ->fromRequest(trinity::core::Json{{"boss_height", 0.5}}),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        requirePart("standoff")->fromRequest(trinity::core::Json{{"height", 0.0}}),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        requirePart("standoff")
            ->fromRequest(trinity::core::Json{{"diameter", 2000.0}}),
        trinity::core::RequestValidationError);
}

TEST_CASE("new parts accept bound values") {
    const trinity::cad::PartDefinition* plate = requirePart("mounting_plate");
    const trinity::cad::PartSpec atBounds = plate->fromRequest(
        trinity::core::Json{{"thickness", 1.0}, {"boss_height", 1.0}});
    CHECK(plate->validator(atBounds, plate->builder(atBounds)).ok);
    const trinity::cad::PartDefinition* post = requirePart("standoff");
    const trinity::cad::PartSpec flush = post->fromRequest(
        trinity::core::Json{{"foot_height", 10.0}});
    CHECK(post->validator(flush, post->builder(flush)).ok);
}

TEST_CASE("new parts reject cross-field violations") {
    CHECK_THROWS_AS(
        requirePart("mounting_plate")
            ->fromRequest(trinity::core::Json{{"boss_inset", 2.0}}),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(
        requirePart("standoff")->fromRequest(trinity::core::Json{{"foot_height", 20.0}}),
        trinity::core::RequestValidationError);
}

TEST_CASE("new parts generate end to end through the engine") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    for (const std::string& type : {"mounting_plate", "standoff"}) {
        trinity::engines::EngineRequest request;
        request.engine = "cad";
        request.operation = "generate";
        request.parameters =
            trinity::core::Json{{"type", type}, {"outputs", {"json"}}};
        const trinity::engines::EngineResult result = registry.execute(request);
        CHECK(result.success);
        CHECK(result.result["type"] == type);
    }
}

TEST_CASE("cad engine refuses empty requests truthfully") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    CHECK_THROWS_AS(registry.execute(trinity::engines::EngineRequest{}),
                    trinity::core::RequestValidationError);
}
