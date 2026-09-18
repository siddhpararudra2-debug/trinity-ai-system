#include "doctest/doctest.h"

#include "../rendering/3d/GeometryLoader.hpp"
#include "../engines/cad/Mesh.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Uuid.hpp"

#include <filesystem>
#include <fstream>

using namespace trinity;

TEST_CASE("geometry loader — STL round-trip via Mesh") {
    engines::cad::Mesh m = engines::cad::make_box({0,0,0},{10,10,2});
    REQUIRE(m.triangles.size() == 12);
    // write to temp
    std::string tmp = (std::filesystem::temp_directory_path() / ("trinity_test_" + core::new_uuid() + ".stl")).string();
    // Use write_binary_stl
    std::error_code ec;
    bool ok = engines::cad::write_binary_stl(m, tmp, ec);
    REQUIRE(ok);
    REQUIRE(!ec);
    auto loaded = rendering::GeometryLoader::load(tmp);
    REQUIRE(loaded.ok);
    CHECK(loaded.format == "stl");
    CHECK(loaded.triangle_count == 12);
    CHECK(loaded.positions.size() == 12*9);
    CHECK(loaded.normals.size() == 12*9);
    // bounding ~ 10mm span
    double sx = loaded.bounding_mm[3]-loaded.bounding_mm[0];
    CHECK(sx == doctest::Approx(10.0).epsilon(0.01));
    std::filesystem::remove(tmp, ec);
}

TEST_CASE("geometry loader — invalid path fails gracefully") {
    auto g = rendering::GeometryLoader::load("C:/no/such/file.stl");
    CHECK(!g.ok);
    CHECK(!g.error.empty());
}

TEST_CASE("geometry loader — GLB scaffold is honest") {
    // Create a tiny fake GLB header to trigger scaffold path
    std::string tmp = (std::filesystem::temp_directory_path() / ("trinity_test_" + core::new_uuid() + ".glb")).string();
    std::ofstream out(tmp, std::ios::binary);
    out.write("glTF",4);
    uint32_t ver=2, len=20;
    out.write(reinterpret_cast<char*>(&ver),4);
    out.write(reinterpret_cast<char*>(&len),4);
    char pad[8]={0};
    out.write(pad,8);
    out.close();
    auto g = rendering::GeometryLoader::load(tmp);
    // expect scaffold error, not ok
    CHECK(!g.ok);
    CHECK(g.error.find("STL instead") != std::string::npos || g.error.find("scaffold") != std::string::npos);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
}

TEST_CASE("geometry loader — from_mesh produces buffers") {
    engines::cad::Mesh m = engines::cad::make_box({0,0,0},{5,5,5});
    auto g = rendering::GeometryLoader::from_mesh(m, "test-id");
    CHECK(g.ok);
    CHECK(g.triangle_count == 12);
    CHECK(g.positions.size() == 36*3); // 12 tris * 3 verts * 3 floats
}
