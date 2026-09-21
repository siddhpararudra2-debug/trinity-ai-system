#pragma once

// Mesh -> GPU-ready render data conversion (Qt-free, headless-testable).
// The viewer uploads RenderData once per model change; per-frame rendering
// never rebuilds buffers. Normals are per-face (flat shading), suitable
// for the box-based CAD backend.

#include <cstdint>
#include <string>
#include <vector>

#include "../cad/Mesh.hpp"

namespace trinity::viewer {

struct RenderData {
    std::vector<float> positions;    // xyz xyz ...
    std::vector<float> normals;      // xyz xyz ... (same length as positions)
    std::vector<std::uint32_t> indices;
    bool valid = false;
    std::string error;

    size_t vertexCount() const { return positions.size() / 3; }
    size_t triangleCount() const { return indices.size() / 3; }
};

/// Convert a Mesh into indexed render data. Never throws: invalid meshes
/// produce {valid=false, error=...} so the UI can show a useful message.
RenderData buildRenderData(const cad::Mesh& mesh) noexcept;

/// Dedupe tolerance in mm for vertex welding.
constexpr double kWeldEpsilonMm = 1e-6;

}  // namespace trinity::viewer
