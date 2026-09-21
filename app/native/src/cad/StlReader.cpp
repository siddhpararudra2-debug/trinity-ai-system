#include "trinity/cad/StlReader.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "trinity/core/Error.hpp"

namespace trinity::cad {
namespace fs = std::filesystem;

namespace {

float readFloatLE(const std::uint8_t* p) {
    std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
                         (static_cast<std::uint32_t>(p[1]) << 8) |
                         (static_cast<std::uint32_t>(p[2]) << 16) |
                         (static_cast<std::uint32_t>(p[3]) << 24);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::uint32_t readU32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool looksLikeAsciiStl(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 6) {
        return false;
    }
    const std::string head(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size() < 128 ? bytes.size() : 128);
    if (head.rfind("solid", 0) != 0) {
        return false;
    }
    const std::string all(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return all.find("facet") != std::string::npos;
}

Mesh decodeBinaryStl(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        throw core::RequestValidationError("Cannot decode empty STL buffer", {}, "viewer");
    }
    if (looksLikeAsciiStl(bytes)) {
        throw core::RequestValidationError(
            "ASCII STL is not supported yet; Trinity writes binary STL only", {}, "viewer");
    }
    if (bytes.size() < 84) {
        throw core::GeometryValidationError(
            "Truncated STL: expected at least 84 bytes, got " + std::to_string(bytes.size()),
            {{"size", static_cast<int>(bytes.size())}}, "viewer");
    }
    const std::uint32_t count = readU32LE(bytes.data() + 80);
    if (count == 0) {
        throw core::GeometryValidationError("STL contains zero triangles", {}, "viewer");
    }
    if (count > 20000000U) {
        throw core::GeometryValidationError(
            "STL triangle count implausibly large: " + std::to_string(count), {}, "viewer");
    }
    const size_t expected = 84 + static_cast<size_t>(count) * 50;
    if (bytes.size() < expected) {
        throw core::GeometryValidationError(
            "Truncated STL: header claims " + std::to_string(count) + " triangles (" +
                std::to_string(expected) + " bytes) but file has " +
                std::to_string(bytes.size()) + " bytes",
            {}, "viewer");
    }
    Mesh mesh;
    mesh.reserveTriangles(count);
    const std::uint8_t* p = bytes.data() + 84;
    for (std::uint32_t i = 0; i < count; ++i) {
        // Skip stored normal; recompute from geometry via triangleNormal().
        p += 12;
        Triangle tri{};
        for (int v = 0; v < 3; ++v) {
            for (int c = 0; c < 3; ++c) {
                tri[static_cast<size_t>(v)][static_cast<size_t>(c)] =
                    static_cast<double>(readFloatLE(p));
                p += 4;
            }
        }
        p += 2;  // attribute word
        for (const auto& v : tri) {
            for (double c : v) {
                if (!std::isfinite(c)) {
                    throw core::GeometryValidationError("STL contains non-finite vertex",
                                                        {}, "viewer");
                }
            }
        }
        mesh.addTriangle(tri);
    }
    if (mesh.empty()) {
        throw core::GeometryValidationError("STL decoded to an empty mesh", {}, "viewer");
    }
    return mesh;
}

Mesh readBinaryStlFile(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        throw core::ArtifactNotFoundError("STL file does not exist: " + path, {}, "viewer");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw core::ArtifactNotFoundError("Cannot open STL file: " + path, {}, "viewer");
    }
    file.seekg(0, std::ios::end);
    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size <= 0 || size > 512 * 1024 * 1024) {
        throw core::GeometryValidationError("STL file has implausible size: " + path, {}, "viewer");
    }
    std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file && !file.eof()) {
        throw core::GeometryValidationError("Failed while reading STL file: " + path, {},
                                            "viewer");
    }
    // Suffix check is advisory only; content determines the error.
    const std::string lower = path;
    if (lower.size() >= 4) {
        const std::string ext = lower.substr(lower.size() - 4);
        if (ext != ".stl" && ext != ".STL") {
            throw core::RequestValidationError("Unsupported mesh format (expected .stl): " + path,
                                               {}, "viewer");
        }
    }
    return decodeBinaryStl(bytes);
}

}  // namespace trinity::cad
