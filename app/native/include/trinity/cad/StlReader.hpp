#pragma once

// Binary STL decoder: inverse of StlWriter::encodeBinaryStl.
// Format-independent viewer architecture: this is one geometry source;
// GLB/gltf deliberately have no decoder here (unsupported_format).

#include <cstdint>
#include <string>
#include <vector>

#include "Mesh.hpp"

namespace trinity::cad {

/// Decode binary STL bytes into a Mesh. Throws GeometryValidationError
/// on truncated/corrupt data, RequestValidationError on empty input.
Mesh decodeBinaryStl(const std::vector<std::uint8_t>& bytes);

/// Read a binary STL file into a Mesh. Throws ArtifactNotFoundError when
/// the file does not exist, GeometryValidationError when corrupt.
/// ASCII STL ("solid ...") is rejected with a clear unsupported message.
Mesh readBinaryStlFile(const std::string& path);

/// True when bytes look like ASCII STL (starts with "solid" and contains
/// "facet"). Used only to produce a useful error message.
bool looksLikeAsciiStl(const std::vector<std::uint8_t>& bytes);

}  // namespace trinity::cad
