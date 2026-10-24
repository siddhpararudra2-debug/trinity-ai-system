#pragma once

// Dependency-free ISO-10303-21 (STEP) writer: faceted boundary
// representation (AP214/AUTOMOTIVE_DESIGN schema) built from tessellated
// triangles — every triangle becomes a FACE_SURFACE over a PLANAR surface
// with a POLY_LOOP outer bound. The output opens in mainstream CAD tools
// as a solid with exact triangle geometry. This is an honest tessellated
// export: it carries the facets (and any planar facetization artifacts the
// source mesh has), not an OCCT-quality analytic B-rep with NURBS; the b-rep
// kernel adapters in the Python cad engine remain the analytic path.

#include <string>

#include "Mesh.hpp"

namespace trinity::cad {

/// Serialize the mesh into an in-memory ISO-10303-21 faceted B-rep string
/// (AP214 schema, mm units). Throws std::runtime_error on empty meshes.
std::string encodeStep(const Mesh& mesh, const std::string& productName);

/// Write an ISO-10303-21 faceted B-rep to `path`. Throws on I/O failure.
void writeStep(const Mesh& mesh, const std::string& path,
               const std::string& productName);

}  // namespace trinity::cad
