// Trinity — GeometryLoader: STL/GLB parsing for the engineering viewport.
// Dependency-free C++ (no Qt) — the QML viewport binds via ViewportController.
// Future: STEP via CadQuery IPC tessellation → STL (never fake STEP).
#pragma once

#include <string>
#include <system_error>
#include <vector>
#include <tuple>

#include "../../engines/cad/Mesh.hpp"

namespace trinity::rendering {

// Result of loading a geometry file for viewport display.
struct LoadedGeometry {
    bool ok = false;
    std::string error;
    std::string format; // "stl" | "glb" | "obj" | ""
    engines::cad::Mesh mesh;
    // Flattened buffers for QQuick3D (positions, normals, indices)
    std::vector<float> positions; // xyz * N
    std::vector<float> normals;   // xyz * N
    std::vector<uint32_t> indices;
    // Metadata
    double bounding_mm[6] = {0,0,0,0,0,0}; // min xyz, max xyz
    size_t triangle_count = 0;
    std::string artifact_id; // when loaded from artifact
};

class GeometryLoader {
public:
    // Loads from an artifact path or arbitrary file. Detects format by extension.
    static LoadedGeometry load(const std::string& file_path);
    static LoadedGeometry load_stl_binary(const std::string& file_path);
    static LoadedGeometry load_stl_ascii(const std::string& file_path) { return load_stl_binary(file_path); } // scaffold
    static LoadedGeometry load_glb(const std::string& file_path);
    static LoadedGeometry load_obj(const std::string& file_path);

    // Creates QQuick3D-compatible buffers from a Mesh (used for native CAD preview without file round-trip).
    static LoadedGeometry from_mesh(const engines::cad::Mesh& mesh, const std::string& artifact_id = {});
};

} // namespace trinity::rendering
