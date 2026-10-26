#pragma once

// Boolean CSG over closed triangle meshes: union / intersection / subtract
// via BSP-tree plane splitting (the classic algorithm behind csg.js —
// well-understood, dependency-free, deterministic for a fixed input order).
// Operands must be closed, consistently outward-oriented meshes produced by
// the part builders; the engine layer refuses results that are not
// watertight. This closes the "overlapping primitives, no CSG" V1 gap under
// the bridge-over-duplication rule: booleans live natively; the Python
// fallback stays V1 as documented.

#include <cstddef>

#include "Mesh.hpp"

namespace trinity::cad {

enum class CsgOperation { Union, Intersect, Subtract };

/// Boolean combination of two closed meshes (mm scale). Empty operands get
/// identity semantics: union returns the other side, subtract of nothing
/// removes nothing, intersection with nothing is empty.
Mesh booleanMesh(const Mesh& a, const Mesh& b, CsgOperation op);

/// Convenience wrappers over booleanMesh.
Mesh csgUnion(const Mesh& a, const Mesh& b);
Mesh csgIntersect(const Mesh& a, const Mesh& b);
Mesh csgSubtract(const Mesh& a, const Mesh& b);

/// Boolean-operand normalization. Part builders glue overlapping closed
/// shells into one triangle soup with Mesh::extend (a standoff is a cylinder
/// plus a coaxial foot, a plate is a box plus raised bosses); BSP clipping
/// assumes each operand behaves like a single solid, and a multi-shell soup
/// produces result boundaries that pair per-shell instead of per-surface.
/// This decomposes the soup into its connected closed shells and folds them
/// pairwise with csgUnion, returning one watertight shell. Single-shell
/// inputs pass through unchanged. Throws std::runtime_error when a fold
/// step cannot produce a watertight result - the honest refusal surface.
Mesh unionMeshComponents(const Mesh& mesh);

/// Signed volume in mm^3 via the divergence theorem. Positive for
/// consistently outward-oriented closed meshes; works on any triangle soup.
double signedVolume(const Mesh& mesh);

/// Watertightness: every undirected edge is shared by exactly two
/// triangles. Vertex endpoints are quantized to a 1e-6 mm grid so
/// split-tolerant neighbor points compare equal.
bool isWatertight(const Mesh& mesh);

}  // namespace trinity::cad
