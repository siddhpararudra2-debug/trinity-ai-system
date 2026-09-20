#include "trinity/cad/StlWriter.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

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

void appendU16LE(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
}

}  // namespace

std::vector<std::uint8_t> encodeBinaryStl(const Mesh& mesh) {
    std::vector<std::uint8_t> out;
    out.reserve(84 + mesh.triangleCount() * 50);
    const char* header = "trinity";
    for (int i = 0; i < 80; ++i) {
        out.push_back(i < 7 ? static_cast<std::uint8_t>(header[i]) : std::uint8_t(0));
    }
    appendU32LE(out, static_cast<std::uint32_t>(mesh.triangleCount()));
    for (const auto& tri : mesh.triangles()) {
        const Vec3 normal = triangleNormal(tri);
        appendFloatLE(out, static_cast<float>(normal[0]));
        appendFloatLE(out, static_cast<float>(normal[1]));
        appendFloatLE(out, static_cast<float>(normal[2]));
        for (const auto& v : tri) {
            appendFloatLE(out, static_cast<float>(v[0]));
            appendFloatLE(out, static_cast<float>(v[1]));
            appendFloatLE(out, static_cast<float>(v[2]));
        }
        appendU16LE(out, 0);
    }
    return out;
}

void writeBinaryStl(const Mesh& mesh, const std::string& path) {
    const std::vector<std::uint8_t> bytes = encodeBinaryStl(mesh);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open STL output path: " + path);
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Failed while writing STL file: " + path);
    }
}

}  // namespace trinity::cad
