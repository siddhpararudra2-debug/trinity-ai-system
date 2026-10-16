#include <doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "trinity/cad/GlbWriter.hpp"
#include "trinity/cad/Parts.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"

// Ports the GLB half of tests/test_regression.py: the native writer must
// emit a standards-compliant glTF binary, and generate() must serve it.

namespace {

std::vector<std::uint8_t> readBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
}

std::uint32_t readU32LE(const std::vector<std::uint8_t>& bytes, size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

}  // namespace

TEST_CASE("glb writer emits a valid glTF binary") {
    const trinity::cad::PartDefinition* part =
        trinity::cad::findPart("quadcopter_frame");
    REQUIRE(part != nullptr);
    const trinity::cad::PartSpec spec =
        part->fromRequest(trinity::core::Json::object());
    const trinity::cad::Mesh mesh = part->builder(spec);
    const std::string path =
        (std::filesystem::temp_directory_path() / "trinity_glb_check.glb").string();
    trinity::cad::writeGlb(mesh, path);
    const std::vector<std::uint8_t> bytes = readBytes(path);
    REQUIRE(bytes.size() > 12);
    CHECK(bytes[0] == 'g');
    CHECK(bytes[1] == 'l');
    CHECK(bytes[2] == 'T');
    CHECK(bytes[3] == 'F');
    CHECK(readU32LE(bytes, 4) == 2);
    CHECK(readU32LE(bytes, 8) == bytes.size());
    std::filesystem::remove(path);
}

TEST_CASE("generate serves stl glb and json together") {
    trinity::engines::EngineRegistry registry;
    trinity::engines::registerAllEngines(registry);
    trinity::engines::EngineRequest request;
    request.engine = "cad";
    request.operation = "generate";
    request.parameters = trinity::core::Json{{"type", "enclosure"},
                                             {"outputs", {"stl", "glb", "json"}}};
    const trinity::engines::EngineResult result = registry.execute(request);
    CHECK(result.success);
    CHECK(result.result["triangle_count"] == 60);
    REQUIRE(result.pendingArtifacts.size() == 3);
    bool sawGlb = false;
    for (const auto& [path, type] : result.pendingArtifacts) {
        if (type == "glb") {
            sawGlb = true;
            const std::vector<std::uint8_t> bytes = readBytes(path);
            REQUIRE(bytes.size() > 12);
            CHECK(bytes[0] == 'g');
            CHECK(readU32LE(bytes, 4) == 2);
        }
    }
    CHECK(sawGlb);
}
