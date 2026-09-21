#include "trinity/viewer/RenderData.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>

namespace trinity::viewer {

RenderData buildRenderData(const cad::Mesh& mesh) noexcept {
    RenderData out;
    try {
        if (mesh.empty()) {
            out.valid = false;
            out.error = "Empty mesh: nothing to render";
            return out;
        }
        const auto& tris = mesh.triangles();
        // Weld identical vertices (box corners are shared across faces).
        std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::uint32_t> indexOf;
        auto keyOf = [](double v) -> std::int64_t {
            return static_cast<std::int64_t>(
                std::llround(v / kWeldEpsilonMm));
        };
        out.positions.reserve(tris.size() * 9);
        out.normals.reserve(tris.size() * 9);
        out.indices.reserve(tris.size() * 3);
        // Flat shading: keep a per-vertex normal equal to the face normal.
        // Weld map stores position->index, but normals array is filled per
        // emitted index the first time; shared corners across faces with
        // different normals keep the first normal (acceptable for boxes).
        std::vector<bool> normalSet;
        for (const auto& tri : tris) {
            for (const auto& v : tri) {
                for (double c : v) {
                    if (!std::isfinite(c)) {
                        out.valid = false;
                        out.error = "Mesh contains non-finite vertex";
                        out.positions.clear();
                        out.normals.clear();
                        out.indices.clear();
                        return out;
                    }
                }
            }
            const cad::Vec3 n = cad::triangleNormal(tri);
            const double area = cad::triangleArea(tri);
            if (!(area > 0.0) || !std::isfinite(n[0] + n[1] + n[2])) {
                // Degenerate triangle: skip but keep mesh renderable.
                continue;
            }
            for (const auto& v : tri) {
                const auto key =
                    std::make_tuple(keyOf(v[0]), keyOf(v[1]), keyOf(v[2]));
                auto it = indexOf.find(key);
                if (it == indexOf.end()) {
                    const auto idx = static_cast<std::uint32_t>(out.positions.size() / 3);
                    out.positions.push_back(static_cast<float>(v[0]));
                    out.positions.push_back(static_cast<float>(v[1]));
                    out.positions.push_back(static_cast<float>(v[2]));
                    out.normals.push_back(static_cast<float>(n[0]));
                    out.normals.push_back(static_cast<float>(n[1]));
                    out.normals.push_back(static_cast<float>(n[2]));
                    normalSet.push_back(true);
                    indexOf.emplace(key, idx);
                    out.indices.push_back(idx);
                } else {
                    out.indices.push_back(it->second);
                }
            }
        }
        if (out.indices.size() < 3) {
            out.valid = false;
            out.error = "Mesh has no renderable triangles (all degenerate?)";
            out.positions.clear();
            out.normals.clear();
            out.indices.clear();
            return out;
        }
        out.valid = true;
        return out;
    } catch (const std::exception& exc) {
        out.valid = false;
        out.error = std::string("Render conversion failed: ") + exc.what();
        out.positions.clear();
        out.normals.clear();
        out.indices.clear();
        return out;
    } catch (...) {
        out.valid = false;
        out.error = "Render conversion failed (unknown error)";
        return out;
    }
}

}  // namespace trinity::viewer
