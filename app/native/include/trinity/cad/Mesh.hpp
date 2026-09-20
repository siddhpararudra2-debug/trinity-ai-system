#pragma once

// Triangle mesh primitives: the dependency-free "local fallback" backend
// (ports src/engines/cad/primitives.py). Axis-aligned boxes, optionally
// rotated about Z — motor mounts render as raised bosses, never bored
// holes, exactly like the Python backend.

#include <array>
#include <string>
#include <vector>

namespace trinity::cad {

using Vec3 = std::array<double, 3>;
using Triangle = std::array<Vec3, 3>;

struct BoundingBox {
    Vec3 min;
    Vec3 max;
    double spanX() const { return max[0] - min[0]; }
    double spanY() const { return max[1] - min[1]; }
    double spanZ() const { return max[2] - min[2]; }
};

class Mesh {
public:
    Mesh() = default;

    void extend(const Mesh& other);
    size_t triangleCount() const { return triangles_.size(); }
    bool empty() const { return triangles_.empty(); }
    const std::vector<Triangle>& triangles() const { return triangles_; }
    BoundingBox boundingBox() const;

private:
    std::vector<Triangle> triangles_;
    friend Mesh box(const Vec3& center, const Vec3& size, double rotationZDeg);
};

/// Box centered at `center` with full-extent `size`, rotated about Z.
Mesh box(const Vec3& center, const Vec3& size, double rotationZDeg = 0.0);

Vec3 triangleNormal(const Triangle& tri);
double triangleArea(const Triangle& tri);

}  // namespace trinity::cad
