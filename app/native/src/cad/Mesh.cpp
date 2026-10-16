#include "trinity/cad/Mesh.hpp"

#include <cmath>
#include <stdexcept>

namespace trinity::cad {

void Mesh::extend(const Mesh& other) {
    triangles_.insert(triangles_.end(), other.triangles_.begin(), other.triangles_.end());
}

void Mesh::addTriangle(const Triangle& tri) { triangles_.push_back(tri); }

void Mesh::reserveTriangles(size_t n) { triangles_.reserve(n); }

void Mesh::clear() { triangles_.clear(); }

BoundingBox Mesh::boundingBox() const {
    if (triangles_.empty()) {
        throw std::invalid_argument("Cannot bound an empty mesh");
    }
    BoundingBox box{triangles_[0][0], triangles_[0][0]};
    for (const auto& tri : triangles_) {
        for (const auto& v : tri) {
            for (int i = 0; i < 3; ++i) {
                if (v[i] < box.min[i]) {
                    box.min[i] = v[i];
                }
                if (v[i] > box.max[i]) {
                    box.max[i] = v[i];
                }
            }
        }
    }
    return box;
}

namespace {

void addFace(std::vector<Triangle>& tris, const Vec3& v0, const Vec3& v1, const Vec3& v2,
             const Vec3& v3) {
    tris.push_back({v0, v1, v2});
    tris.push_back({v0, v2, v3});
}

}  // namespace

Mesh box(const Vec3& center, const Vec3& size, double rotationZDeg) {
    const double hx = size[0] / 2.0;
    const double hy = size[1] / 2.0;
    const double hz = size[2] / 2.0;
    const double theta = rotationZDeg * 3.14159265358979323846 / 180.0;
    const double cosT = std::cos(theta);
    const double sinT = std::sin(theta);

    const Vec3 local[8] = {{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz},
                           {-hx, -hy, hz},  {hx, -hy, hz},  {hx, hy, hz},  {-hx, hy, hz}};
    Vec3 c[8];
    for (int i = 0; i < 8; ++i) {
        const double rx = local[i][0] * cosT - local[i][1] * sinT;
        const double ry = local[i][0] * sinT + local[i][1] * cosT;
        c[i] = {rx + center[0], ry + center[1], local[i][2] + center[2]};
    }

    Mesh out;
    // Bottom/top/front/right/back/left — same winding as the Python backend.
    std::vector<Triangle> tris;
    tris.reserve(12);
    addFace(tris, c[0], c[1], c[2], c[3]);  // bottom
    addFace(tris, c[7], c[6], c[5], c[4]);  // top
    addFace(tris, c[4], c[5], c[1], c[0]);  // front
    addFace(tris, c[5], c[6], c[2], c[1]);  // right
    addFace(tris, c[6], c[7], c[3], c[2]);  // back
    addFace(tris, c[7], c[4], c[0], c[3]);  // left
    for (const auto& tri : tris) {
        out.triangles_.push_back(tri);
    }
    return out;
}

Mesh cylinder(const Vec3& center, double radius, double height, int segments) {
    if (radius <= 0.0) {
        throw std::invalid_argument("radius must be > 0");
    }
    if (height <= 0.0) {
        throw std::invalid_argument("height must be > 0");
    }
    if (segments < 8) {
        throw std::invalid_argument("segments must be >= 8");
    }
    constexpr double kPi = 3.14159265358979323846;
    const double z0 = center[2] - height / 2.0;
    const double z1 = center[2] + height / 2.0;

    std::vector<Vec3> bottom;
    std::vector<Vec3> top;
    bottom.reserve(static_cast<size_t>(segments));
    top.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const double theta = 2.0 * kPi * i / segments;
        const double px = center[0] + radius * std::cos(theta);
        const double py = center[1] + radius * std::sin(theta);
        bottom.push_back({px, py, z0});
        top.push_back({px, py, z1});
    }

    Mesh out;
    out.reserveTriangles(static_cast<size_t>(4 * segments));
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        // Sides: CCW seen from outside, normal points away from the axis.
        out.triangles_.push_back({bottom[i], bottom[j], top[j]});
        out.triangles_.push_back({bottom[i], top[j], top[i]});
        // Caps: top wound +Z, bottom wound -Z.
        out.triangles_.push_back({top[i], top[j], {center[0], center[1], z1}});
        out.triangles_.push_back({bottom[j], bottom[i], {center[0], center[1], z0}});
    }
    return out;
}

Vec3 triangleNormal(const Triangle& tri) {
    const double ux = tri[1][0] - tri[0][0];
    const double uy = tri[1][1] - tri[0][1];
    const double uz = tri[1][2] - tri[0][2];
    const double vx = tri[2][0] - tri[0][0];
    const double vy = tri[2][1] - tri[0][1];
    const double vz = tri[2][2] - tri[0][2];
    double nx = uy * vz - uz * vy;
    double ny = uz * vx - ux * vz;
    double nz = ux * vy - uy * vx;
    const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length == 0.0) {
        return {0.0, 0.0, 0.0};
    }
    nx /= length;
    ny /= length;
    nz /= length;
    return {nx, ny, nz};
}

double triangleArea(const Triangle& tri) {
    const double ux = tri[1][0] - tri[0][0];
    const double uy = tri[1][1] - tri[0][1];
    const double uz = tri[1][2] - tri[0][2];
    const double vx = tri[2][0] - tri[0][0];
    const double vy = tri[2][1] - tri[0][1];
    const double vz = tri[2][2] - tri[0][2];
    const double cx = uy * vz - uz * vy;
    const double cy = uz * vx - ux * vz;
    const double cz = ux * vy - uy * vx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

}  // namespace trinity::cad
