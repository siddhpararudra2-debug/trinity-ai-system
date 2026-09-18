#include "Mesh.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "../../core/FileSystem.hpp"

namespace trinity::engines::cad {
namespace {

constexpr double kDegenerateAreaEpsilon = 1e-9;

std::tuple<double, double, double> face_normal(const Vec3& v0, const Vec3& v1, const Vec3& v2) {
    const auto [x0, y0, z0] = v0;
    const auto [x1, y1, z1] = v1;
    const auto [x2, y2, z2] = v2;
    const double ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
    const double vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length < kDegenerateAreaEpsilon) return {0.0, 0.0, 1.0};
    return {nx / length, ny / length, nz / length};
}

void append_face(std::vector<Triangle>& out, const Vec3& v0, const Vec3& v1, const Vec3& v2,
                 const Vec3& v3) {
    out.emplace_back(v0, v1, v2);
    out.emplace_back(v0, v2, v3);
}

void write_u32_le(std::FILE* file, std::uint32_t value) {
    unsigned char bytes[4] = {static_cast<unsigned char>(value & 0xFF),
                              static_cast<unsigned char>((value >> 8) & 0xFF),
                              static_cast<unsigned char>((value >> 16) & 0xFF),
                              static_cast<unsigned char>((value >> 24) & 0xFF)};
    std::fwrite(bytes, 1, 4, file);
}

void write_u16_le(std::FILE* file, std::uint16_t value) {
    unsigned char bytes[2] = {static_cast<unsigned char>(value & 0xFF),
                              static_cast<unsigned char>((value >> 8) & 0xFF)};
    std::fwrite(bytes, 1, 2, file);
}

void write_f32_le(std::FILE* file, double value) {
    const float f = static_cast<float>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &f, 4);
    write_u32_le(file, bits);
}

}  // namespace

std::pair<Vec3, Vec3> Mesh::bounding_box() const {
    double min_x = 0, min_y = 0, min_z = 0, max_x = 0, max_y = 0, max_z = 0;
    bool first = true;
    for (const Triangle& triangle : triangles) {
        for (const Vec3& vertex : {std::get<0>(triangle), std::get<1>(triangle),
                                   std::get<2>(triangle)}) {
            const auto [x, y, z] = vertex;
            if (first) {
                min_x = max_x = x;
                min_y = max_y = y;
                min_z = max_z = z;
                first = false;
                continue;
            }
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
            min_z = std::min(min_z, z); max_z = std::max(max_z, z);
        }
    }
    return {{min_x, min_y, min_z}, {max_x, max_y, max_z}};
}

void Mesh::extend(const Mesh& other) {
    triangles.insert(triangles.end(), other.triangles.begin(), other.triangles.end());
}

Mesh make_box(const Vec3& center, const Vec3& size, double rotation_z_degrees) {
    const auto [cx, cy, cz] = center;
    const auto [sx, sy, sz] = size;
    const double hx = sx / 2.0, hy = sy / 2.0, hz = sz / 2.0;

    const Vec3 local_corners[8] = {
        {-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
        {-hx, -hy, hz},  {hx, -hy, hz},  {hx, hy, hz},  {-hx, hy, hz},
    };

    const double theta = rotation_z_degrees * 3.14159265358979323846 / 180.0;
    const double cos_t = std::cos(theta);
    const double sin_t = std::sin(theta);

    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const auto [x, y, z] = local_corners[i];
        const double rx = x * cos_t - y * sin_t;
        const double ry = x * sin_t + y * cos_t;
        corners[i] = {rx + cx, ry + cy, z + cz};
    }

    Mesh mesh;
    mesh.triangles.reserve(12);
    append_face(mesh.triangles, corners[0], corners[1], corners[2], corners[3]);  // bottom
    append_face(mesh.triangles, corners[7], corners[6], corners[5], corners[4]);  // top
    append_face(mesh.triangles, corners[4], corners[5], corners[1], corners[0]);  // front
    append_face(mesh.triangles, corners[5], corners[6], corners[2], corners[1]);  // right
    append_face(mesh.triangles, corners[6], corners[7], corners[3], corners[2]);  // back
    append_face(mesh.triangles, corners[7], corners[4], corners[0], corners[3]);  // left
    return mesh;
}

bool write_binary_stl(const Mesh& mesh, const std::string& path, std::error_code& ec) {
    std::FILE* file = nullptr;
#if defined(_WIN32)
    if (fopen_s(&file, path.c_str(), "wb") != 0) file = nullptr;
#else
    file = std::fopen(path.c_str(), "wb");
#endif
    if (file == nullptr) {
        ec = std::make_error_code(std::errc::io_error);
        return false;
    }

    const std::string header = "trinity";
    unsigned char header_bytes[80] = {0};
    std::memcpy(header_bytes, header.data(), std::min(header.size(), std::size_t(79)));
    std::fwrite(header_bytes, 1, 80, file);
    write_u32_le(file, static_cast<std::uint32_t>(mesh.triangles.size()));

    for (const Triangle& triangle : mesh.triangles) {
        const auto& [v0, v1, v2] = triangle;
        const auto [nx, ny, nz] = face_normal(v0, v1, v2);
        write_f32_le(file, nx);
        write_f32_le(file, ny);
        write_f32_le(file, nz);
        for (const Vec3& v : {v0, v1, v2}) {
            write_f32_le(file, std::get<0>(v));
            write_f32_le(file, std::get<1>(v));
            write_f32_le(file, std::get<2>(v));
        }
        write_u16_le(file, 0);
    }
    std::fclose(file);
    return true;
}

}  // namespace trinity::engines::cad
