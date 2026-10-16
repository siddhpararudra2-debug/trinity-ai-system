#pragma once

// Dependency-free GLB 2.0 exporter (ports src/engines/cad/glb.py):
// unindexed triangle positions/normals, byte-identical layout to the
// Python backend (same generator tag, same compact JSON, same padding).

#include <string>

#include "Mesh.hpp"

namespace trinity::cad {

/// Throws std::runtime_error when the output cannot be written.
void writeGlb(const Mesh& mesh, const std::string& path);

}  // namespace trinity::cad
