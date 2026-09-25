#pragma once

// Clean artifact-loading path for the viewer (Qt-free).
// Format-independent: callers ask for a Mesh by artifact id or file path;
// this layer picks the right decoder. GLB/gltf have no decoder and report
// unsupported_format truthfully — never fake geometry.

#include <string>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../cad/Mesh.hpp"
#include "../core/Json.hpp"
#include "../validation/ValidationResult.hpp"

namespace trinity::viewer {

struct LoadedModel {
    cad::Mesh mesh;
    artifacts::Artifact artifact;
    validation::ValidationResult validation;
    core::Json jobResult = core::Json::object();
    std::string source;  // "memory" | "stl" | "spec-json"
};

/// Rebuild the exact validated mesh from a CAD job result envelope
/// (result.spec as produced by CadEngine). Throws on bad input.
/// This is the preferred path: no file IO, uses the real parameters.
cad::Mesh rebuildMeshFromJobResult(const core::Json& jobResult);

/// Rebuild from a spec JSON object (params.toJson() shape or flat params).
cad::Mesh rebuildMeshFromSpec(const core::Json& spec);

/// Synthesize a robot arm mesh from a RoboticsEngine FK/IK result payload
/// (IR `frames` object + `frame_order`, or legacy DH `frames` array with
/// `end_effector`). Links become oriented boxes between consecutive
/// frames with joint cubes at each frame — visualization only, never
/// analysis geometry. Throws on bad input.
cad::Mesh buildRobotMeshFromResult(const core::Json& result);

/// Load a mesh from a managed artifact file on disk, with integrity checks:
/// file must exist, size must be > 0 and match metadata when provided,
/// extension must be a supported mesh format (.stl) or spec (.json).
/// Unsupported formats (.glb/.gltf/.step) throw CapabilityUnavailableError.
LoadedModel loadModelFromArtifactFile(const artifacts::Artifact& artifact);

/// Load from an arbitrary file path with an explicit type hint
/// ("stl" | "json"). Used by tests and the file-reopen path.
LoadedModel loadModelFromPath(const std::string& path, const std::string& typeHint,
                              std::string jobId = "");

/// Basic integrity gate used by the UI before attempting a load:
/// exists on disk, non-empty, supported extension. Returns empty string
/// when OK, otherwise a human-readable reason.
std::string checkArtifactIntegrity(const artifacts::Artifact& artifact) noexcept;

/// Supported mesh extensions for reopen (lowercase, with dot).
bool isSupportedMeshExtension(const std::string& path) noexcept;
bool isSpecJsonExtension(const std::string& path) noexcept;
bool isUnsupportedFormat(const std::string& path) noexcept;

/// Human-readable one-line summary for artifact metadata display.
std::string formatArtifactSummary(const artifacts::Artifact& artifact);

}  // namespace trinity::viewer
