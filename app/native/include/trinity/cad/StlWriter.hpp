#pragma once

// Binary STL export (ports src/engines/cad/primitives.write_binary_stl):
// 80-byte `trinity` header, little-endian triangle count, per-triangle
// normal + vertices + attribute word. Written with explicit byte order
// so output is identical on every platform.

#include <cstdint>
#include <string>
#include <vector>

#include "Mesh.hpp"

namespace trinity::cad {

/// Serialize a mesh to binary STL bytes.
std::vector<std::uint8_t> encodeBinaryStl(const Mesh& mesh);

/// Write a mesh to `path` as binary STL. Throws on I/O failure.
void writeBinaryStl(const Mesh& mesh, const std::string& path);

}  // namespace trinity::cad
