#pragma once

// Viewer state model: current mesh + camera + display modes + loading/error.
// Deliberately separate from the CAD engine: the viewer never generates
// geometry, it only visualizes Mesh objects handed to it. Qt-free so camera
// fit/reset math is headless-testable.

#include <optional>
#include <string>

#include "../artifacts/Artifact.hpp"
#include "../cad/Mesh.hpp"
#include "../validation/ValidationResult.hpp"
#include "Measure.hpp"
#include "RenderData.hpp"

namespace trinity::viewer {

enum class ProjectionMode {
    Perspective,
    Orthographic,
};

enum class RenderMode {
    Solid,
    Wireframe,
};

struct Camera {
    cad::Vec3 position{80.0, 80.0, 60.0};
    cad::Vec3 target{0.0, 0.0, 0.0};
    double fovDeg = 45.0;
    double nearMm = 0.5;
    double farMm = 10000.0;
    double orthoHalfHeightMm = 60.0;
};

inline const char* toString(ProjectionMode mode) noexcept {
    return mode == ProjectionMode::Perspective ? "Perspective" : "Orthographic";
}

inline const char* toString(RenderMode mode) noexcept {
    return mode == RenderMode::Solid ? "Solid" : "Wireframe";
}

class ViewerState {
public:
    ViewerState() = default;

    // --- Model ---
    void setMesh(cad::Mesh mesh, artifacts::Artifact artifact,
                 validation::ValidationResult validation);
    void setMeshOnly(cad::Mesh mesh);
    void clearMesh();
    bool hasMesh() const noexcept { return mesh_.has_value(); }
    const std::optional<cad::Mesh>& mesh() const noexcept { return mesh_; }
    const RenderData& renderData() const noexcept { return render_; }
    bool hasBoundingBox() const noexcept { return bbox_.has_value(); }
    const std::optional<cad::BoundingBox>& boundingBox() const noexcept { return bbox_; }

    // --- Artifact / validation ---
    const artifacts::Artifact& artifact() const noexcept { return artifact_; }
    bool hasArtifact() const noexcept { return hasArtifact_; }
    const validation::ValidationResult& validation() const noexcept { return validation_; }
    void setValidation(validation::ValidationResult v) { validation_ = std::move(v); }

    // --- Display modes ---
    ProjectionMode projection() const noexcept { return projection_; }
    void setProjection(ProjectionMode m) noexcept { projection_ = m; }
    RenderMode renderMode() const noexcept { return renderMode_; }
    void setRenderMode(RenderMode m) noexcept { renderMode_ = m; }
    bool gridEnabled() const noexcept { return grid_; }
    void setGrid(bool on) noexcept { grid_ = on; }
    bool axesEnabled() const noexcept { return axes_; }
    void setAxes(bool on) noexcept { axes_ = on; }

    // --- Loading / error ---
    bool loading() const noexcept { return loading_; }
    void setLoading(bool on) noexcept { loading_ = on; }
    const std::string& error() const noexcept { return error_; }
    bool hasError() const noexcept { return !error_.empty(); }
    void setError(std::string msg) { error_ = std::move(msg); }
    void clearError() { error_.clear(); }

    // --- Camera ---
    const Camera& camera() const noexcept { return camera_; }
    Camera& camera() noexcept { return camera_; }
    void resetCamera();
    void fitToModel();

    // --- Measurement ---
    Measurement& measurement() noexcept { return measurement_; }
    const Measurement& measurement() const noexcept { return measurement_; }

private:
    std::optional<cad::Mesh> mesh_;
    RenderData render_;
    std::optional<cad::BoundingBox> bbox_;
    artifacts::Artifact artifact_;
    bool hasArtifact_ = false;
    validation::ValidationResult validation_;
    ProjectionMode projection_ = ProjectionMode::Perspective;
    RenderMode renderMode_ = RenderMode::Solid;
    bool grid_ = true;
    bool axes_ = true;
    bool loading_ = false;
    std::string error_;
    Camera camera_;
    Measurement measurement_;
};

}  // namespace trinity::viewer
