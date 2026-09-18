// Trinity — CAD engine tests (semantics parity with backend/tests/test_cad.py).
#include <filesystem>
#include <fstream>

#include <doctest/doctest.h>

#include "../core/FileSystem.hpp"
#include "../engines/cad/CadEngine.hpp"
#include "../engines/cad/Mesh.hpp"
#include "../engines/cad/QuadcopterFrame.hpp"

using namespace trinity::engines::cad;

namespace {
std::string scratch_dir() {
    return std::filesystem::temp_directory_path().string() + "/trinity_test_cad";
}
}  // namespace

TEST_CASE("default IR produces the flagship 50 mm frame") {
    auto ir = QuadcopterFrameIR::from_request(core::Json::parse("{}"));
    CHECK(ir.parameters().at("overall_size") == doctest::Approx(50.0));
    CHECK(ir.parameters().at("center_plate_size") == doctest::Approx(26.0));
    Mesh mesh = build_quadcopter_frame(ir);
    CHECK(mesh.triangle_count() == 12 * 9);  // plate 12 + 4 arms*12 + 4 bosses*12
    auto report = validate_quadcopter_frame(ir, mesh);
    CHECK(report.passed);
    CHECK(report.checks.find("dimensions_within_tolerance")->as_bool());
    CHECK(report.checks.find("degenerate_triangle_count")->as_int() == 0);
}

TEST_CASE("binary STL byte layout matches the V1 Python writer") {
    auto ir = QuadcopterFrameIR::from_request(core::Json::parse(R"({"overall_size": 50})"));
    Mesh mesh = build_quadcopter_frame(ir);

    const std::string dir = scratch_dir();
    std::error_code ec;
    REQUIRE(core::FileSystem::ensure_directory(dir, ec));

    const std::string path = dir + "/frame.stl";
    REQUIRE(write_binary_stl(mesh, path, ec));

    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::uint32_t count = mesh.triangle_count();
    CHECK(data.size() == 84 + static_cast<std::size_t>(count) * 50);
    // Header is 'trinity' then zeros.
    CHECK(data.substr(0, 7) == "trinity");
    CHECK(static_cast<unsigned char>(data[80]) == (count & 0xFF));
    CHECK(static_cast<unsigned char>(data[81]) == ((count >> 8) & 0xFF));
    // Attribute byte count of the first triangle is zero.
    CHECK(static_cast<unsigned char>(data[83]) == 0);
    CHECK(static_cast<unsigned char>(data[82]) == 0);
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("IR rejects invalid requests exactly like the Python version") {
    CHECK_THROWS(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"unknown_param": 1})")));
    CHECK_THROWS(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"overall_size": -5})")));
    CHECK_THROWS(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"overall_size": 10})")));  // smaller than plate
    CHECK_THROWS(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"overall_size": 5000})")));  // outside V1 range
    CHECK_THROWS(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"fc_mount_spacing": 30})")));  // exceeds plate
    CHECK_NOTHROW(QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"overall_size": 80, "arm_width": 6})")));
}

TEST_CASE("validator fails degenerate manufacturability") {
    auto ir = QuadcopterFrameIR::from_request(
        core::Json::parse(R"({"arm_width": 0.5})"));  // below FDM floor
    Mesh mesh = build_quadcopter_frame(ir);
    auto report = validate_quadcopter_frame(ir, mesh);
    CHECK_FALSE(report.passed);
    CHECK_FALSE(report.checks.find("manufacturable_min_feature")->as_bool());
}

TEST_CASE("mesh kernel adapter imports its own STL round trip") {
    auto ir = QuadcopterFrameIR::from_request(core::Json::parse("{}"));
    Mesh mesh = build_quadcopter_frame(ir);

    const std::string dir = scratch_dir();
    std::error_code ec;
    REQUIRE(core::FileSystem::ensure_directory(dir, ec));
    const std::string path = dir + "/round.stl";
    REQUIRE(write_binary_stl(mesh, path, ec));

    MeshKernelAdapter adapter;
    Mesh imported;
    REQUIRE(adapter.import_model(path, imported));
    CHECK(imported.triangle_count() == mesh.triangle_count());
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("cad engine generate reports unavailable formats honestly") {
    CadEngine engine;
    core::Json request = core::Json::parse(
        R"({"type": "quadcopter_frame", "parameters": {"overall_size": 50}, "outputs": ["stl", "json", "step"]})");
    auto output = engine.execute("generate", request);
    CHECK(output.success);
    CHECK(output.validation_status == "VALIDATED");
    CHECK(output.pending_artifacts.size() == 2);  // stl + json
    CHECK(output.result.contains("unavailable_formats"));
    CHECK(output.result.find("unavailable_formats")->contains("step"));
}

TEST_CASE("cad engine refuses unknown types and capabilities") {
    CadEngine engine;
    CHECK_THROWS(engine.execute("generate", core::Json::parse(R"({"type": "drone_body"})")));
    CHECK_THROWS(engine.execute("teleport", core::Json::parse("{}")));
}

TEST_CASE("scaffold engines refuse execution honestly") {
    auto registered = trinity::engines::bootstrap_builtin_engines();
    REQUIRE(registered.size() >= 8);
    auto pcb = trinity::engines::EngineRegistry::instance().get("pcb");
    REQUIRE(pcb.is_ok());
    CHECK(pcb.value()->health() == trinity::engines::EngineHealth::Scaffolded);
    CHECK_THROWS(pcb.value()->execute("generate", core::Json::parse("{}")));
}
