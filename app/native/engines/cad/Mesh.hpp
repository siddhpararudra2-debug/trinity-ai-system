// Trinity — C++ mesh primitives. Port of backend/app/engines/cad/primitives.py
// with identical triangle layout and STL byte format (asserted by tests).
#pragma once

#include <string>
#include <tuple>
#include <vector>

namespace trinity::engines::cad {

using Vec3 = std::tuple<double, double, double>;
using Triangle = std::tuple<Vec3, Vec3, Vec3>;

class Mesh {
public:
    std::vector<Triangle> triangles;

    // (min, max) over all vertices.
    std::pair<Vec3, Vec3> bounding_box() const;
    void extend(const Mesh& other);
    std::size_t triangle_count() const { return triangles.size(); }
};

// Axis-aligned box (optionally rotated about Z), consistent winding.
Mesh make_box(const Vec3& center, const Vec3& size, double rotation_z_degrees = 0.0);

// Binary STL with the exact V1 layout: 80-byte header, uint32 triangle count,
// per-triangle normal + 3 vertices (float32), uint16 attribute byte count.
bool write_binary_stl(const Mesh& mesh, const std::string& path, std::error_code& ec);

}  // namespace trinity::engines::cad
