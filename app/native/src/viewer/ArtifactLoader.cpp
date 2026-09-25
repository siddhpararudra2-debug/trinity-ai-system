#include "trinity/viewer/ArtifactLoader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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

namespace {

using Point = std::array<double, 3>;

bool readPoint(const core::Json& node, Point& out) {
    if (!node.is_object() || !node.contains("x") || !node.contains("y") ||
        !node.contains("z")) {
        return false;
    }
    if (!node["x"].is_number() || !node["y"].is_number() || !node["z"].is_number()) {
        return false;
    }
    out = {node["x"].get<double>(), node["y"].get<double>(), node["z"].get<double>()};
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

double pointDistance(const Point& a, const Point& b) {
    const double dx = b[0] - a[0];
    const double dy = b[1] - a[1];
    const double dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

Point pointSub(const Point& a, const Point& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }

Point pointCross(const Point& a, const Point& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

Point pointNormalized(const Point& v) {
    const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!(n > 0.0)) {
        return {0.0, 0.0, 0.0};
    }
    return {v[0] / n, v[1] / n, v[2] / n};
}

void addQuad(cad::Mesh& mesh, const Point& a, const Point& b, const Point& c, const Point& d) {
    mesh.addTriangle({a, b, c});
    mesh.addTriangle({a, c, d});
}

/// Box from a to b with square cross-section of half side `half`.
void addSegmentBox(cad::Mesh& mesh, const Point& a, const Point& b, double half) {
    const Point d = pointNormalized(pointSub(b, a));
    Point up{0.0, 0.0, 1.0};
    if (std::fabs(d[0] * up[0] + d[1] * up[1] + d[2] * up[2]) > 0.9) {
        up = {0.0, 1.0, 0.0};
    }
    const Point w = pointNormalized(pointCross(d, up));
    const Point u = pointCross(w, d);
    const Point base[2] = {a, b};
    Point corner[2][4];
    for (int e = 0; e < 2; ++e) {
        for (int k = 0; k < 4; ++k) {
            const double su = (k == 0 || k == 3) ? half : -half;
            const double sw = (k < 2) ? half : -half;
            corner[e][k] = {base[e][0] + u[0] * su + w[0] * sw,
                            base[e][1] + u[1] * su + w[1] * sw,
                            base[e][2] + u[2] * su + w[2] * sw};
        }
    }
    for (int k = 0; k < 4; ++k) {
        const int next = (k + 1) % 4;
        addQuad(mesh, corner[0][k], corner[0][next], corner[1][next], corner[1][k]);
    }
    addQuad(mesh, corner[0][0], corner[0][1], corner[0][2], corner[0][3]);
    addQuad(mesh, corner[1][0], corner[1][3], corner[1][2], corner[1][1]);
}

}  // namespace

cad::Mesh buildRobotMeshFromResult(const core::Json& result) {
    if (!result.is_object()) {
        throw core::RequestValidationError("Robot result is not a JSON object", {}, "viewer");
    }
    std::vector<Point> points;
    if (result.contains("frames") && result["frames"].is_object()) {
        const core::Json& frames = result["frames"];
        if (!result.contains("frame_order") || !result["frame_order"].is_array()) {
            throw core::RequestValidationError(
                "Robot result has frames but no frame_order to chain them", {}, "viewer");
        }
        for (const core::Json& name : result["frame_order"]) {
            if (!name.is_string()) {
                continue;
            }
            const std::string key = name.get<std::string>();
            if (!frames.contains(key) || !frames[key].is_object() ||
                !frames[key].contains("position")) {
                continue;
            }
            Point p;
            if (readPoint(frames[key]["position"], p)) {
                points.push_back(p);
            }
        }
    } else if (result.contains("frames") && result["frames"].is_array()) {
        points.push_back({0.0, 0.0, 0.0});
        for (const core::Json& frame : result["frames"]) {
            if (!frame.is_object() || !frame.contains("position")) {
                continue;
            }
            Point p;
            if (readPoint(frame["position"], p)) {
                points.push_back(p);
            }
        }
        if (result.contains("end_effector") && result["end_effector"].is_object() &&
            result["end_effector"].contains("position")) {
            Point ee;
            if (readPoint(result["end_effector"]["position"], ee) &&
                (points.empty() || pointDistance(points.back(), ee) > 1e-12)) {
                points.push_back(ee);
            }
        }
    } else {
        throw core::RequestValidationError("Robot result has no frames to visualize", {},
                                           "viewer");
    }
    if (points.size() < 2) {
        throw core::RequestValidationError("Robot result has fewer than 2 frames", {},
                                           "viewer");
    }
    double span = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        span = std::max(span, pointDistance(points[0], points[i]));
    }
    if (!(span > 0.0)) {
        throw core::RequestValidationError("Robot frames are all coincident", {}, "viewer");
    }
    cad::Mesh mesh;
    const double linkHalf = span * 0.015;
    for (size_t i = 1; i < points.size(); ++i) {
        if (pointDistance(points[i - 1], points[i]) > span * 1e-9) {
            addSegmentBox(mesh, points[i - 1], points[i], linkHalf);
        }
    }
    for (size_t i = 0; i < points.size(); ++i) {
        const double half = (i == 0 ? span * 0.03 : span * 0.018);
        mesh.extend(cad::box(points[i], {half * 2.0, half * 2.0, half * 2.0}));
    }
    if (mesh.empty()) {
        throw core::RequestValidationError("Robot mesh synthesis produced no geometry", {},
                                           "viewer");
    }
    return mesh;
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
