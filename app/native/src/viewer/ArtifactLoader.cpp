#include "trinity/viewer/ArtifactLoader.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/StlReader.hpp"
#include "trinity/cad/Validators.hpp"
#include "trinity/core/Error.hpp"

namespace trinity::viewer {
namespace fs = std::filesystem;

namespace {

std::string lowerExt(const std::string& path) {
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

core::Json specParamsRaw(const core::Json& spec) {
    // CadEngine stores spec = params.toJson() = {type,units,parameters:{...}}.
    // FrameParams::fromRequest wants the flat {...} map.
    if (spec.is_object() && spec.contains("parameters") && spec["parameters"].is_object()) {
        return spec["parameters"];
    }
    return spec;
}

validation::ValidationResult validationForMesh(const cad::Mesh& mesh,
                                               const core::Json& checks,
                                               const std::string& jobId) {
    validation::ValidationResult out;
    out.jobId = jobId;
    out.operation = "generate";
    out.checks = checks.is_object() ? checks : core::Json::object();
    if (mesh.empty()) {
        out.status = validation::ValidationStatus::Invalid;
        out.message = "Loaded mesh is empty";
    } else {
        out.status = validation::ValidationStatus::Validated;
        out.message = "Reopened artifact passed basic integrity checks";
    }
    return out;
}

}  // namespace

bool isSupportedMeshExtension(const std::string& path) noexcept {
    return lowerExt(path) == ".stl";
}

bool isSpecJsonExtension(const std::string& path) noexcept {
    return lowerExt(path) == ".json";
}

bool isUnsupportedFormat(const std::string& path) noexcept {
    const std::string ext = lowerExt(path);
    return ext == ".glb" || ext == ".gltf" || ext == ".step" || ext == ".stp" ||
           ext == ".obj" || ext == ".ply" || ext == ".3mf";
}

std::string checkArtifactIntegrity(const artifacts::Artifact& artifact) noexcept {
    try {
        if (artifact.path.empty()) {
            return "Artifact has no file path";
        }
        std::error_code ec;
        if (!fs::is_regular_file(artifact.path, ec) || ec) {
            return "Artifact file does not exist: " + artifact.path;
        }
        const auto size = fs::file_size(artifact.path, ec);
        if (ec) {
            return "Cannot stat artifact file: " + artifact.path;
        }
        if (size == 0) {
            return "Artifact file is empty: " + artifact.path;
        }
        if (artifact.sizeBytes > 0 &&
            static_cast<std::uint64_t>(artifact.sizeBytes) != size) {
            return "Artifact size mismatch (metadata " + std::to_string(artifact.sizeBytes) +
                   " vs file " + std::to_string(size) + ")";
        }
        if (isSupportedMeshExtension(artifact.path) || isSpecJsonExtension(artifact.path)) {
            return "";
        }
        if (isUnsupportedFormat(artifact.path)) {
            return "Unsupported format '" + lowerExt(artifact.path) +
                   "': no decoder in this build (STL/spec-JSON only)";
        }
        return "Unsupported artifact format: " + lowerExt(artifact.path);
    } catch (...) {
        return "Integrity check failed unexpectedly";
    }
}

cad::Mesh rebuildMeshFromSpec(const core::Json& spec) {
    const core::Json raw = specParamsRaw(spec);
    const cad::FrameParams params = cad::FrameParams::fromRequest(raw);
    cad::Mesh mesh = cad::buildQuadcopterFrame(params);
    const cad::FrameValidation check = cad::validateQuadcopterFrame(params, mesh);
    if (!check.ok) {
        throw core::GeometryValidationError("Rebuilt geometry failed validation",
                                            {{"checks", check.checks}}, "viewer");
    }
    return mesh;
}

cad::Mesh rebuildMeshFromJobResult(const core::Json& jobResult) {
    if (!jobResult.is_object() || !jobResult.contains("spec")) {
        throw core::RequestValidationError("Job result has no CAD spec to rebuild from", {},
                                           "viewer");
    }
    return rebuildMeshFromSpec(jobResult["spec"]);
}

LoadedModel loadModelFromPath(const std::string& path, const std::string& typeHint,
                              std::string jobId) {
    if (path.empty()) {
        throw core::ArtifactNotFoundError("No artifact path provided", {}, "viewer");
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        throw core::ArtifactNotFoundError("Artifact file does not exist: " + path, {}, "viewer");
    }
    if (fs::file_size(path, ec) == 0 || ec) {
        throw core::GeometryValidationError("Artifact file is empty: " + path, {}, "viewer");
    }
    std::string hint = typeHint;
    std::transform(hint.begin(), hint.end(), hint.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string ext = lowerExt(path);
    const bool wantJson = hint == "json" || (hint.empty() && ext == ".json");
    const bool wantStl = hint == "stl" || hint == "mesh" || (hint.empty() && ext == ".stl");

    LoadedModel out;
    out.artifact.path = path;
    out.artifact.jobId = std::move(jobId);
    out.artifact.type = wantJson ? "json" : "stl";
    out.artifact.sizeBytes = static_cast<long long>(fs::file_size(path, ec));

    if (wantJson && !wantStl) {
        std::ifstream in(path);
        if (!in) {
            throw core::ArtifactNotFoundError("Cannot open spec file: " + path, {}, "viewer");
        }
        core::Json spec = core::Json::parse(in, nullptr, false);
        if (spec.is_discarded()) {
            throw core::GeometryValidationError("Spec JSON is corrupt: " + path, {}, "viewer");
        }
        // Spec files store params.toJson(); job-result shape wraps in {spec}.
        core::Json specObj = spec;
        if (spec.contains("spec")) {
            specObj = spec["spec"];
        }
        out.mesh = rebuildMeshFromSpec(specObj);
        out.source = "spec-json";
        out.validation = validationForMesh(out.mesh, core::Json::object(), out.artifact.jobId);
        return out;
    }
    if (wantStl) {
        out.mesh = cad::readBinaryStlFile(path);
        out.source = "stl";
        out.validation = validationForMesh(out.mesh, core::Json::object(), out.artifact.jobId);
        return out;
    }
    throw core::CapabilityUnavailableError(
        "Unsupported mesh format '" + ext + "': this build decodes binary STL and spec JSON only",
        {{"path", path}}, "viewer");
}

LoadedModel loadModelFromArtifactFile(const artifacts::Artifact& artifact) {
    const std::string problem = checkArtifactIntegrity(artifact);
    if (!problem.empty()) {
        if (isUnsupportedFormat(artifact.path) ||
            (!isSupportedMeshExtension(artifact.path) &&
             !isSpecJsonExtension(artifact.path))) {
            throw core::CapabilityUnavailableError(problem, {{"path", artifact.path}}, "viewer");
        }
        // Missing/empty/mismatch -> integrity error, not a format issue.
        if (artifact.path.empty() ||
            problem.find("does not exist") != std::string::npos ||
            problem.find("empty") != std::string::npos ||
            problem.find("mismatch") != std::string::npos ||
            problem.find("Cannot stat") != std::string::npos) {
            throw core::ArtifactNotFoundError(problem, {}, "viewer");
        }
        throw core::GeometryValidationError(problem, {}, "viewer");
    }
    LoadedModel out = loadModelFromPath(artifact.path, artifact.type, artifact.jobId);
    out.artifact = artifact;
    out.validation.jobId = artifact.jobId;
    return out;
}

std::string formatArtifactSummary(const artifacts::Artifact& artifact) {
    std::string name = artifact.path;
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    if (name.empty()) {
        name = artifact.artifactId.empty() ? "(unnamed)" : artifact.artifactId.substr(0, 8);
    }
    return name + " [" + artifact.type + ", " + std::to_string(artifact.sizeBytes) + " B]";
}

}  // namespace trinity::viewer
