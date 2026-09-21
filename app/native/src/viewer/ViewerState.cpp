#include "trinity/viewer/ViewerState.hpp"

#include <cmath>

namespace trinity::viewer {

namespace {

cad::Vec3 bboxCenter(const cad::BoundingBox& box) {
    return {(box.min[0] + box.max[0]) / 2.0, (box.min[1] + box.max[1]) / 2.0,
            (box.min[2] + box.max[2]) / 2.0};
}

double bboxRadius(const cad::BoundingBox& box) {
    const double dx = box.spanX();
    const double dy = box.spanY();
    const double dz = box.spanZ();
    return 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

void ViewerState::setMesh(cad::Mesh mesh, artifacts::Artifact artifact,
                          validation::ValidationResult validation) {
    try {
        bbox_ = mesh.boundingBox();
    } catch (...) {
        bbox_.reset();
    }
    render_ = buildRenderData(mesh);
    if (!render_.valid) {
        setError(render_.error.empty() ? "Cannot render mesh" : render_.error);
    } else {
        clearError();
    }
    mesh_ = std::move(mesh);
    artifact_ = std::move(artifact);
    hasArtifact_ = true;
    validation_ = std::move(validation);
    fitToModel();
}

void ViewerState::setMeshOnly(cad::Mesh mesh) {
    artifacts::Artifact artifact;
    validation::ValidationResult validation;
    validation.status = validation::ValidationStatus::Generated;
    validation.message = "Preview geometry (no artifact selected)";
    setMesh(std::move(mesh), std::move(artifact), std::move(validation));
    hasArtifact_ = false;
}

void ViewerState::clearMesh() {
    mesh_.reset();
    bbox_.reset();
    render_ = RenderData{};
    hasArtifact_ = false;
    artifact_ = artifacts::Artifact{};
    validation_ = validation::ValidationResult{};
    measurement_.clear();
}

void ViewerState::resetCamera() {
    if (!bbox_.has_value()) {
        camera_ = Camera{};
        return;
    }
    fitToModel();
}

void ViewerState::fitToModel() {
    if (!bbox_.has_value()) {
        camera_ = Camera{};
        return;
    }
    const cad::Vec3 center = bboxCenter(*bbox_);
    const double radius = bboxRadius(*bbox_);
    const double safeRadius = radius > 1e-9 ? radius : 10.0;
    constexpr double kPi = 3.14159265358979323846;
    const double halfFov = (camera_.fovDeg / 2.0) * kPi / 180.0;
    const double dist = (safeRadius / std::tan(halfFov)) * 1.35;
    // Keep current orbit direction; default to isometric when degenerate.
    cad::Vec3 dir{camera_.position[0] - camera_.target[0],
                  camera_.position[1] - camera_.target[1],
                  camera_.position[2] - camera_.target[2]};
    double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 1e-9) || !std::isfinite(len)) {
        dir = {1.0, 1.0, 0.75};
        len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    }
    dir[0] /= len;
    dir[1] /= len;
    dir[2] /= len;
    camera_.target = center;
    camera_.position = {center[0] + dir[0] * dist, center[1] + dir[1] * dist,
                        center[2] + dir[2] * dist};
    camera_.orthoHalfHeightMm = safeRadius * 1.35;
    const double span = std::max({bbox_->spanX(), bbox_->spanY(), bbox_->spanZ(), 1.0});
    camera_.nearMm = std::max(dist - span * 4.0, 0.1);
    camera_.farMm = dist + span * 20.0 + 1000.0;
}

}  // namespace trinity::viewer
