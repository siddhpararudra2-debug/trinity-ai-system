#include <doctest.h>

#include <filesystem>
#include <fstream>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/StlReader.hpp"
#include "trinity/cad/StlWriter.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/validation/ValidationResult.hpp"
#include "trinity/viewer/ArtifactLoader.hpp"

namespace fs = std::filesystem;

namespace {

trinity::cad::Mesh makeFrame() {
    const auto params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    return trinity::cad::buildQuadcopterFrame(params);
}

std::string tempDir(const std::string& name) {
    const std::string dir = (fs::temp_directory_path() / name).string();
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

}  // namespace

TEST_CASE("viewer: binary STL round-trips the real 108-triangle frame") {
    const auto mesh = makeFrame();
    const auto bytes = trinity::cad::encodeBinaryStl(mesh);
    REQUIRE(bytes.size() == 84 + 50 * 108);
    const auto decoded = trinity::cad::decodeBinaryStl(bytes);
    CHECK(decoded.triangleCount() == 108);
    const auto a = mesh.boundingBox();
    const auto b = decoded.boundingBox();
    CHECK(b.spanX() == doctest::Approx(a.spanX()).epsilon(1e-5));
    CHECK(b.spanY() == doctest::Approx(a.spanY()).epsilon(1e-5));
    CHECK(b.spanZ() == doctest::Approx(a.spanZ()).epsilon(1e-5));
}

TEST_CASE("viewer: corrupt STL produces useful errors, never a crash") {
    CHECK_THROWS_AS(trinity::cad::decodeBinaryStl({}),
                    trinity::core::RequestValidationError);
    CHECK_THROWS_AS(trinity::cad::decodeBinaryStl(std::vector<std::uint8_t>(10, 0)),
                    trinity::core::GeometryValidationError);
    // Header claims 5 triangles but body is truncated.
    std::vector<std::uint8_t> truncated(84 + 2 * 50, 0);
    truncated[80] = 5;
    CHECK_THROWS_AS(trinity::cad::decodeBinaryStl(truncated),
                    trinity::core::GeometryValidationError);
    // ASCII STL is rejected with a clear message, not decoded as binary.
    const std::string ascii =
        "solid trinity\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\n"
        "vertex 0 1 0\nendloop\nendfacet\nendsolid\n";
    CHECK_THROWS_AS(
        trinity::cad::decodeBinaryStl(
            std::vector<std::uint8_t>(ascii.begin(), ascii.end())),
        trinity::core::RequestValidationError);
    // Missing file is ArtifactNotFoundError, not a crash.
    CHECK_THROWS_AS(
        trinity::cad::readBinaryStlFile("/nonexistent-dir/missing.stl"),
        trinity::core::ArtifactNotFoundError);
}

TEST_CASE("viewer: artifact file loading prefers real files, rejects fake GLB") {
    const std::string dir = tempDir("trinity-test-viewer-load");
    const std::string stlPath = dir + "/frame.stl";
    trinity::cad::writeBinaryStl(makeFrame(), stlPath);

    const auto fromStl =
        trinity::viewer::loadModelFromPath(stlPath, "stl", "job-1");
    CHECK(fromStl.mesh.triangleCount() == 108);
    CHECK(fromStl.source == "stl");

    // Spec-JSON reopen path rebuilds the exact mesh without a CAD engine call.
    const std::string jsonPath = dir + "/frame.json";
    {
        const auto params =
            trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
        std::ofstream out(jsonPath, std::ios::binary | std::ios::trunc);
        out << params.toJson().dump(2);
    }
    const auto fromJson =
        trinity::viewer::loadModelFromPath(jsonPath, "json", "job-1");
    CHECK(fromJson.mesh.triangleCount() == 108);
    CHECK(fromJson.source == "spec-json");

    // No fake GLB support: .glb reports unsupported_format truthfully.
    const std::string glbPath = dir + "/frame.glb";
    {
        std::ofstream out(glbPath, std::ios::binary | std::ios::trunc);
        out << "glTF";
    }
    CHECK_THROWS_AS(trinity::viewer::loadModelFromPath(glbPath, "glb"),
                    trinity::core::CapabilityUnavailableError);
    CHECK(trinity::viewer::isUnsupportedFormat(glbPath));
    CHECK_FALSE(trinity::viewer::isSupportedMeshExtension(glbPath));

    // Missing artifact is a clean error.
    CHECK_THROWS_AS(trinity::viewer::loadModelFromPath(dir + "/nope.stl", "stl"),
                    trinity::core::ArtifactNotFoundError);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("viewer: integrity gate blocks missing/empty/mismatched artifacts") {
    trinity::artifacts::Artifact missing;
    missing.path = "/nonexistent/missing.stl";
    missing.sizeBytes = 100;
    CHECK_FALSE(trinity::viewer::checkArtifactIntegrity(missing).empty());

    const std::string dir = tempDir("trinity-test-viewer-integrity");
    const std::string stlPath = dir + "/a.stl";
    trinity::cad::writeBinaryStl(makeFrame(), stlPath);
    trinity::artifacts::Artifact good;
    good.artifactId = "abc123";
    good.jobId = "job-1";
    good.type = "stl";
    good.path = stlPath;
    good.sizeBytes = static_cast<long long>(fs::file_size(stlPath));
    CHECK(trinity::viewer::checkArtifactIntegrity(good).empty());

    trinity::artifacts::Artifact mismatch = good;
    mismatch.sizeBytes = good.sizeBytes + 1;
    CHECK_FALSE(trinity::viewer::checkArtifactIntegrity(mismatch).empty());

    trinity::artifacts::Artifact glb = good;
    glb.path = dir + "/a.glb";
    {
        std::ofstream out(glb.path, std::ios::binary | std::ios::trunc);
        out << "glTF";
    }
    glb.sizeBytes = static_cast<long long>(fs::file_size(glb.path));
    CHECK_FALSE(trinity::viewer::checkArtifactIntegrity(glb).empty());

    // Metadata display shows name/type/size without crashing.
    const std::string summary = trinity::viewer::formatArtifactSummary(good);
    CHECK(summary.find("a.stl") != std::string::npos);
    CHECK(summary.find("stl") != std::string::npos);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST_CASE("viewer: job-result rebuild uses the real spec, never placeholder") {
    const auto params =
        trinity::cad::FrameParams::fromRequest(trinity::core::Json::object());
    const trinity::core::Json jobResult = {{"spec", params.toJson()},
                                           {"triangle_count", 108}};
    const auto mesh = trinity::viewer::rebuildMeshFromJobResult(jobResult);
    CHECK(mesh.triangleCount() == 108);

    CHECK_THROWS_AS(
        trinity::viewer::rebuildMeshFromJobResult(trinity::core::Json::object()),
        trinity::core::RequestValidationError);
}

TEST_CASE("viewer: validation states never show invalid as verified") {
    using trinity::validation::ValidationStatus;
    CHECK(trinity::validation::toString(ValidationStatus::Generated) == "GENERATED");
    CHECK(trinity::validation::toString(ValidationStatus::Validated) == "VALIDATED");
    CHECK(trinity::validation::toString(ValidationStatus::Verified) == "VERIFIED");
    CHECK(trinity::validation::toString(ValidationStatus::Invalid) == "INVALID");

    trinity::validation::ValidationResult ok;
    ok.status = ValidationStatus::Validated;
    CHECK(ok.passed());
    trinity::validation::ValidationResult verified;
    verified.status = ValidationStatus::Verified;
    CHECK(verified.passed());
    trinity::validation::ValidationResult bad;
    bad.status = ValidationStatus::Invalid;
    CHECK_FALSE(bad.passed());
    trinity::validation::ValidationResult gen;
    gen.status = ValidationStatus::Generated;
    CHECK_FALSE(gen.passed());
    // FAILED is an alias of INVALID and must not pass either.
    CHECK(trinity::validation::toString(ValidationStatus::Failed) == "INVALID");
}
