#include "trinity/cad/GlbWriter.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "trinity/core/Json.hpp"

namespace trinity::cad {

namespace {

void appendFloatLE(std::vector<std::uint8_t>& out, float value) {
    static_assert(sizeof(float) == 4, "float must be 32-bit");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    out.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xFFU));
}

void appendU32LE(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
}

void appendBytes(std::vector<std::uint8_t>& out, const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<std::uint8_t>(data[i]));
    }
}

}  // namespace

void writeGlb(const Mesh& mesh, const std::string& path) {
    const auto& tris = mesh.triangles();
    const size_t vertexCount = tris.size() * 3;

    std::vector<std::uint8_t> positions;
    positions.reserve(vertexCount * 12);
    std::vector<std::uint8_t> normalBytes;
    normalBytes.reserve(vertexCount * 12);
    Vec3 minV = tris.empty() ? Vec3{0.0, 0.0, 0.0} : tris[0][0];
    Vec3 maxV = minV;
    for (const auto& tri : tris) {
        const Vec3 n = triangleNormal(tri);
        for (const auto& v : tri) {
            appendFloatLE(positions, static_cast<float>(v[0]));
            appendFloatLE(positions, static_cast<float>(v[1]));
            appendFloatLE(positions, static_cast<float>(v[2]));
            appendFloatLE(normalBytes, static_cast<float>(n[0]));
            appendFloatLE(normalBytes, static_cast<float>(n[1]));
            appendFloatLE(normalBytes, static_cast<float>(n[2]));
            for (int i = 0; i < 3; ++i) {
                if (v[i] < minV[i]) {
                    minV[i] = v[i];
                }
                if (v[i] > maxV[i]) {
                    maxV[i] = v[i];
                }
            }
        }
    }

    std::vector<std::uint8_t> binary;
    binary.reserve(positions.size() + normalBytes.size());
    binary.insert(binary.end(), positions.begin(), positions.end());
    binary.insert(binary.end(), normalBytes.begin(), normalBytes.end());

    const core::Json document = {
        {"asset", {{"version", "2.0"}, {"generator", "Trinity AI native CAD"}}},
        {"scene", 0},
        {"scenes", core::Json::array({{{"nodes", {0}}}})},
        {"nodes", core::Json::array({{{"mesh", 0}}})},
        {"meshes",
         core::Json::array(
             {{{"primitives",
                core::Json::array(
                    {{{"attributes", {{"POSITION", 0}, {"NORMAL", 1}}},
                      {"mode", 4}}})}}})},
        {"buffers", core::Json::array({{{"byteLength", binary.size()}}})},
        {"bufferViews",
         core::Json::array(
             {{{"buffer", 0},
               {"byteOffset", 0},
               {"byteLength", positions.size()},
               {"target", 34962}},
              {{"buffer", 0},
               {"byteOffset", positions.size()},
               {"byteLength", normalBytes.size()},
               {"target", 34962}}})},
        {"accessors",
         core::Json::array(
             {{{"bufferView", 0},
               {"componentType", 5126},
               {"count", vertexCount},
               {"type", "VEC3"},
               {"min", {minV[0], minV[1], minV[2]}},
               {"max", {maxV[0], maxV[1], maxV[2]}}},
              {{"bufferView", 1},
               {"componentType", 5126},
               {"count", vertexCount},
               {"type", "VEC3"}}})},
    };
    std::string encoded = document.dump();
    while (encoded.size() % 4 != 0) {
        encoded.push_back(' ');
    }
    while (binary.size() % 4 != 0) {
        binary.push_back(0);
    }

    std::vector<std::uint8_t> out;
    const std::uint32_t total =
        static_cast<std::uint32_t>(12 + 8 + encoded.size() + 8 + binary.size());
    appendBytes(out, "glTF", 4);
    appendU32LE(out, 2);
    appendU32LE(out, total);
    appendU32LE(out, static_cast<std::uint32_t>(encoded.size()));
    appendBytes(out, "JSON", 4);
    appendBytes(out, encoded.data(), encoded.size());
    appendU32LE(out, static_cast<std::uint32_t>(binary.size()));
    appendBytes(out, "BIN\0", 4);
    out.insert(out.end(), binary.begin(), binary.end());

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open GLB output path: " + path);
    }
    file.write(reinterpret_cast<const char*>(out.data()),
               static_cast<std::streamsize>(out.size()));
    if (!file) {
        throw std::runtime_error("Failed while writing GLB file: " + path);
    }
}

}  // namespace trinity::cad
