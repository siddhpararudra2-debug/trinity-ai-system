#pragma once

// Reusable Qt 6 native 3D viewer viewport (QOpenGLWidget).
// Visualizes real CAD RenderData only — never generates geometry and never
// shows placeholder geometry when a real artifact is available. Empty state
// is a grid + "No model loaded" overlay.
// Controls: Left=orbit, Middle=pan, Wheel=zoom, F=fit, R=reset.

#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>

#include <memory>
#include <optional>

#include "trinity/cad/Mesh.hpp"
#include "trinity/viewer/RenderData.hpp"
#include "trinity/viewer/ViewerState.hpp"

namespace trinity::ui {

class ViewportWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    // Upload once per model change (not per frame). Safe to call with no GL
    // context yet — upload is deferred until the next paint.
    void setRenderData(const viewer::RenderData& renderData,
                       const std::optional<cad::BoundingBox>& bbox);
    bool hasModel() const;

    void setProjectionMode(viewer::ProjectionMode mode);
    void setRenderMode(viewer::RenderMode mode);
    void setGridEnabled(bool on);
    void setAxesEnabled(bool on);

    void setCamera(const viewer::Camera& cam);
    viewer::Camera camera() const;
    void fitModel();
    void resetCamera();

    void setMeasureMode(bool on);
    bool measureMode() const { return measureMode_; }
    void clearMeasurement();

    // Last GPU/render error (empty when healthy).
    QString lastError() const { return lastError_; }

signals:
    void measurementChanged(double ax, double ay, double az, double bx, double by,
                            double bz, bool complete);
    void renderError(const QString& message);
    void cameraChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void uploadPending();
    void destroyGlResources();
    void ensureShaders();
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix() const;
    QVector3D cameraPosition() const;
    void setCameraFromSpherical();
    void updateFromCameraPosition(const QVector3D& pos, const QVector3D& target);
    void pickVertex(const QPoint& clickPos);
    void rebuildGrid();
    QPointF projectToScreen(const QVector3D& world) const;

    viewer::RenderData pendingData_;
    bool pendingUpload_ = false;
    bool glReady_ = false;
    bool hasModel_ = false;

    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;
    std::unique_ptr<QOpenGLShaderProgram> lineProgram_;
    std::unique_ptr<QOpenGLVertexArrayObject> vao_;
    std::unique_ptr<QOpenGLBuffer> vbo_;
    std::unique_ptr<QOpenGLBuffer> ebo_;
    size_t indexCount_ = 0;

    // Grid + axes line geometry (world-space xyz pairs).
    std::unique_ptr<QOpenGLVertexArrayObject> lineVao_;
    std::unique_ptr<QOpenGLBuffer> lineVbo_;
    std::vector<float> gridLines_;
    std::vector<float> axesLines_;

    viewer::ProjectionMode projection_ = viewer::ProjectionMode::Perspective;
    viewer::RenderMode renderMode_ = viewer::RenderMode::Solid;
    bool grid_ = true;
    bool axes_ = true;

    // Orbit camera: target + yaw/pitch/distance (+ ortho half-height).
    QVector3D target_{0.0F, 0.0F, 0.0F};
    double yawDeg_ = 45.0;
    double pitchDeg_ = 28.0;
    double distance_ = 160.0;
    double orthoHalf_ = 60.0;
    std::optional<cad::BoundingBox> bbox_;

    bool measureMode_ = false;
    std::optional<QVector3D> measureA_;
    std::optional<QVector3D> measureB_;
    // CPU copy of positions for picking (float xyz).
    std::vector<float> positions_;

    QPoint lastPos_;
    bool orbiting_ = false;
    bool panning_ = false;

    QString lastError_;
};

}  // namespace trinity::ui
