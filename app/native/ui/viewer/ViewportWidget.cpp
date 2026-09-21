#include "ViewportWidget.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace trinity::ui {

namespace {

constexpr double kFovDeg = 45.0;

QVector3D toQ(const std::array<double, 3>& v) {
    return QVector3D(static_cast<float>(v[0]), static_cast<float>(v[1]),
                     static_cast<float>(v[2]));
}

}  // namespace

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

ViewportWidget::~ViewportWidget() {
    makeCurrent();
    destroyGlResources();
    doneCurrent();
}

bool ViewportWidget::hasModel() const { return hasModel_; }

void ViewportWidget::setRenderData(const viewer::RenderData& renderData,
                                   const std::optional<cad::BoundingBox>& bbox) {
    pendingData_ = renderData;
    bbox_ = bbox;
    pendingUpload_ = true;
    positions_ = renderData.positions;
    if (!renderData.valid) {
        lastError_ = QString::fromStdString(renderData.error);
        emit renderError(lastError_);
    } else {
        lastError_.clear();
    }
    update();
}

void ViewportWidget::setProjectionMode(viewer::ProjectionMode mode) {
    projection_ = mode;
    update();
}

void ViewportWidget::setRenderMode(viewer::RenderMode mode) {
    renderMode_ = mode;
    update();
}

void ViewportWidget::setGridEnabled(bool on) {
    grid_ = on;
    update();
}

void ViewportWidget::setAxesEnabled(bool on) {
    axes_ = on;
    update();
}

void ViewportWidget::setCamera(const viewer::Camera& cam) {
    updateFromCameraPosition(toQ(cam.position), toQ(cam.target));
    orthoHalf_ = cam.orthoHalfHeightMm;
    update();
}

viewer::Camera ViewportWidget::camera() const {
    viewer::Camera cam;
    const QVector3D pos = cameraPosition();
    cam.position = {static_cast<double>(pos.x()), static_cast<double>(pos.y()),
                    static_cast<double>(pos.z())};
    cam.target = {static_cast<double>(target_.x()), static_cast<double>(target_.y()),
                  static_cast<double>(target_.z())};
    cam.fovDeg = kFovDeg;
    cam.orthoHalfHeightMm = orthoHalf_;
    return cam;
}

void ViewportWidget::fitModel() {
    if (!bbox_.has_value()) {
        return;
    }
    const auto& box = *bbox_;
    target_ = QVector3D(static_cast<float>((box.min[0] + box.max[0]) / 2.0),
                        static_cast<float>((box.min[1] + box.max[1]) / 2.0),
                        static_cast<float>((box.min[2] + box.max[2]) / 2.0));
    const double dx = box.spanX();
    const double dy = box.spanY();
    const double dz = box.spanZ();
    const double radius = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
    const double safe = radius > 1e-9 ? radius : 10.0;
    const double halfFov = (kFovDeg / 2.0) * 3.141592653589793 / 180.0;
    distance_ = (safe / std::tan(halfFov)) * 1.35;
    distance_ = std::clamp(distance_, 2.0, 20000.0);
    orthoHalf_ = safe * 1.35;
    rebuildGrid();
    update();
    emit cameraChanged();
}

void ViewportWidget::resetCamera() {
    yawDeg_ = 45.0;
    pitchDeg_ = 28.0;
    fitModel();
}

void ViewportWidget::setMeasureMode(bool on) { measureMode_ = on; }

void ViewportWidget::clearMeasurement() {
    measureA_.reset();
    measureB_.reset();
    emit measurementChanged(0, 0, 0, 0, 0, 0, false);
    update();
}

// --- GL lifecycle: upload only on model change, never per frame ---

void ViewportWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10F, 0.10F, 0.12F, 1.0F);
    glEnable(GL_DEPTH_TEST);
    glReady_ = true;
    ensureShaders();
    if (pendingUpload_) {
        uploadPending();
    }
    rebuildGrid();
}

void ViewportWidget::resizeGL(int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    glViewport(0, 0, w, h);
}

void ViewportWidget::ensureShaders() {
    if (meshProgram_) {
        return;
    }
    try {
        meshProgram_ = std::make_unique<QOpenGLShaderProgram>(this);
        const char* vSrc =
            "#version 330 core\n"
            "layout(location = 0) in vec3 pos;\n"
            "layout(location = 1) in vec3 nrm;\n"
            "uniform mat4 mvp;\n"
            "out vec3 vNormal;\n"
            "void main() { gl_Position = mvp * vec4(pos, 1.0); vNormal = nrm; }\n";
        const char* fSrc =
            "#version 330 core\n"
            "in vec3 vNormal;\n"
            "uniform vec3 baseColor;\n"
            "uniform vec3 lightDir;\n"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "  vec3 n = normalize(vNormal);\n"
            "  float diff = max(dot(n, normalize(lightDir)), 0.0);\n"
            "  vec3 col = baseColor * (0.35 + 0.65 * diff);\n"
            "  fragColor = vec4(col, 1.0);\n"
            "}\n";
        if (!meshProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, vSrc) ||
            !meshProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, fSrc) ||
            !meshProgram_->link()) {
            lastError_ = QStringLiteral("Mesh shader failed: ") + meshProgram_->log();
            emit renderError(lastError_);
            meshProgram_.reset();
        }

        lineProgram_ = std::make_unique<QOpenGLShaderProgram>(this);
        const char* lvSrc =
            "#version 330 core\n"
            "layout(location = 0) in vec3 pos;\n"
            "uniform mat4 mvp;\n"
            "void main() { gl_Position = mvp * vec4(pos, 1.0); }\n";
        const char* lfSrc =
            "#version 330 core\n"
            "uniform vec3 lineColor;\n"
            "out vec4 fragColor;\n"
            "void main() { fragColor = vec4(lineColor, 1.0); }\n";
        if (!lineProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, lvSrc) ||
            !lineProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, lfSrc) ||
            !lineProgram_->link()) {
            lastError_ = QStringLiteral("Line shader failed: ") + lineProgram_->log();
            emit renderError(lastError_);
            lineProgram_.reset();
        }

        vao_ = std::make_unique<QOpenGLVertexArrayObject>(this);
        vao_->create();
        vbo_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        vbo_->create();
        ebo_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer);
        ebo_->create();
        lineVao_ = std::make_unique<QOpenGLVertexArrayObject>(this);
        lineVao_->create();
        lineVbo_ = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
        lineVbo_->create();
    } catch (const std::exception& exc) {
        lastError_ = QString::fromStdString(std::string("GL init failed: ") + exc.what());
        emit renderError(lastError_);
    }
}

void ViewportWidget::uploadPending() {
    pendingUpload_ = false;
    if (!glReady_ || !meshProgram_) {
        return;
    }
    try {
        if (!pendingData_.valid || pendingData_.indices.empty()) {
            hasModel_ = false;
            indexCount_ = 0;
            update();
            return;
        }
        // Interleave positions + normals.
        const size_t verts = pendingData_.vertexCount();
        std::vector<float> interleaved;
        interleaved.reserve(verts * 6);
        for (size_t i = 0; i < verts; ++i) {
            interleaved.push_back(pendingData_.positions[i * 3 + 0]);
            interleaved.push_back(pendingData_.positions[i * 3 + 1]);
            interleaved.push_back(pendingData_.positions[i * 3 + 2]);
            interleaved.push_back(pendingData_.normals[i * 3 + 0]);
            interleaved.push_back(pendingData_.normals[i * 3 + 1]);
            interleaved.push_back(pendingData_.normals[i * 3 + 2]);
        }
        makeCurrent();
        vao_->bind();
        vbo_->bind();
        vbo_->allocate(interleaved.data(),
                       static_cast<int>(interleaved.size() * sizeof(float)));
        meshProgram_->bind();
        meshProgram_->enableAttributeArray(0);
        meshProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 6 * sizeof(float));
        meshProgram_->enableAttributeArray(1);
        meshProgram_->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3,
                                         6 * sizeof(float));
        meshProgram_->release();
        ebo_->bind();
        ebo_->allocate(pendingData_.indices.data(),
                       static_cast<int>(pendingData_.indices.size() * sizeof(uint32_t)));
        vao_->release();
        indexCount_ = pendingData_.indices.size();
        hasModel_ = true;
        lastError_.clear();
        // Auto-fit on new model so the 50mm frame is immediately viewable.
        fitModel();
    } catch (const std::exception& exc) {
        lastError_ = QString::fromStdString(std::string("GPU upload failed: ") + exc.what());
        emit renderError(lastError_);
        hasModel_ = false;
    }
    update();
}

void ViewportWidget::destroyGlResources() {
    vao_.reset();
    vbo_.reset();
    ebo_.reset();
    lineVao_.reset();
    lineVbo_.reset();
    meshProgram_.reset();
    lineProgram_.reset();
}

QVector3D ViewportWidget::cameraPosition() const {
    const double yaw = yawDeg_ * 3.141592653589793 / 180.0;
    const double pitch = pitchDeg_ * 3.141592653589793 / 180.0;
    const float x = static_cast<float>(distance_ * std::cos(pitch) * std::cos(yaw));
    const float y = static_cast<float>(distance_ * std::cos(pitch) * std::sin(yaw));
    const float z = static_cast<float>(distance_ * std::sin(pitch));
    return target_ + QVector3D(x, y, z);
}

QMatrix4x4 ViewportWidget::viewMatrix() const {
    QMatrix4x4 view;
    const QVector3D eye = cameraPosition();
    QVector3D up(0.0F, 0.0F, 1.0F);
    if (std::abs(pitchDeg_) > 85.0) {
        up = QVector3D(0.0F, 1.0F, 0.0F);
    }
    view.lookAt(eye, target_, up);
    return view;
}

QMatrix4x4 ViewportWidget::projectionMatrix() const {
    QMatrix4x4 proj;
    const float aspect =
        height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0F;
    if (projection_ == viewer::ProjectionMode::Perspective) {
        const float nearP = static_cast<float>(std::max(distance_ / 1000.0, 0.1));
        const float farP = static_cast<float>(distance_ + 20000.0);
        proj.perspective(static_cast<float>(kFovDeg), aspect, nearP, farP);
    } else {
        const float half = static_cast<float>(orthoHalf_);
        const float nearP = -20000.0F;
        const float farP = 20000.0F;
        proj.ortho(-half * aspect, half * aspect, -half, half, nearP, farP);
    }
    return proj;
}

void ViewportWidget::rebuildGrid() {
    gridLines_.clear();
    axesLines_.clear();
    double span = 100.0;
    double minZ = 0.0;
    if (bbox_.has_value()) {
        span = std::max({bbox_->spanX(), bbox_->spanY(), bbox_->spanZ(), 10.0});
        minZ = bbox_->min[2];
    }
    const double half = span * 1.2;
    const double step = std::max(span / 10.0, 1.0);
    const float z = static_cast<float>(minZ - span * 0.02);
    for (double c = -half; c <= half + 0.5 * step; c += step) {
        gridLines_.insert(gridLines_.end(),
                          {static_cast<float>(-half), static_cast<float>(c), z,
                           static_cast<float>(half), static_cast<float>(c), z});
        gridLines_.insert(gridLines_.end(),
                          {static_cast<float>(c), static_cast<float>(-half), z,
                           static_cast<float>(c), static_cast<float>(half), z});
    }
    const float ax = static_cast<float>(span * 0.7);
    axesLines_.insert(axesLines_.end(), {0, 0, 0, ax, 0, 0});  // X red
    axesLines_.insert(axesLines_.end(), {0, 0, 0, 0, ax, 0});  // Y green
    axesLines_.insert(axesLines_.end(), {0, 0, 0, 0, 0, ax});  // Z blue
}

void ViewportWidget::paintGL() {
    try {
        if (pendingUpload_) {
            uploadPending();
        }
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (renderMode_ == viewer::RenderMode::Wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        } else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        const QMatrix4x4 mvp = projectionMatrix() * viewMatrix();

        // Mesh.
        if (hasModel_ && meshProgram_ && indexCount_ > 0) {
            meshProgram_->bind();
            meshProgram_->setUniformValue("mvp", mvp);
            meshProgram_->setUniformValue("baseColor", QVector3D(0.55F, 0.70F, 0.95F));
            const QVector3D eye = cameraPosition();
            QVector3D light = (eye - target_);
            if (light.lengthSquared() < 1e-6F) {
                light = QVector3D(1, 1, 2);
            }
            meshProgram_->setUniformValue("lightDir", light.normalized());
            vao_->bind();
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT,
                           nullptr);
            vao_->release();
            meshProgram_->release();
        }
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        // Grid + axes lines.
        if (lineProgram_) {
            lineProgram_->bind();
            lineProgram_->setUniformValue("mvp", mvp);
            lineVao_->bind();
            lineVbo_->bind();
            auto drawLines = [&](const std::vector<float>& lines, QVector3D color) {
                if (lines.empty()) {
                    return;
                }
                lineVbo_->allocate(lines.data(),
                                   static_cast<int>(lines.size() * sizeof(float)));
                lineProgram_->enableAttributeArray(0);
                lineProgram_->setAttributeBuffer(0, GL_FLOAT, 0, 3, 3 * sizeof(float));
                lineProgram_->setUniformValue("lineColor", color);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size() / 3));
            };
            if (grid_) {
                drawLines(gridLines_, QVector3D(0.30F, 0.30F, 0.33F));
            }
            if (axes_) {
                // Draw each axis separately for color.
                if (axesLines_.size() >= 18) {
                    std::vector<float> one(axesLines_.begin(), axesLines_.begin() + 6);
                    drawLines(one, QVector3D(0.95F, 0.25F, 0.25F));
                    one.assign(axesLines_.begin() + 6, axesLines_.begin() + 12);
                    drawLines(one, QVector3D(0.25F, 0.85F, 0.35F));
                    one.assign(axesLines_.begin() + 12, axesLines_.begin() + 18);
                    drawLines(one, QVector3D(0.30F, 0.55F, 1.0F));
                }
            }
            lineVbo_->release();
            lineVao_->release();
            lineProgram_->release();
        }
    } catch (...) {
        lastError_ = QStringLiteral("Rendering failed (GPU error)");
        emit renderError(lastError_);
    }

    // 2D overlay: empty-state text, measurement markers, error banner.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!hasModel_ && !pendingUpload_) {
        painter.setPen(QColor(160, 160, 170));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No model loaded"));
    }
    if (!lastError_.isEmpty()) {
        painter.setPen(QColor(255, 120, 120));
        painter.drawText(QRect(12, 8, width() - 24, 40), Qt::AlignLeft | Qt::TextWordWrap,
                         lastError_);
    }
    auto drawMarker = [&](const std::optional<QVector3D>& m, const QColor& c) {
        if (!m.has_value()) {
            return QPoint();
        }
        const QPointF s = projectToScreen(*m);
        painter.setBrush(c);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(s, 5, 5);
        return QPoint(static_cast<int>(s.x()), static_cast<int>(s.y()));
    };
    if (measureA_.has_value() || measureB_.has_value()) {
        const QPoint pa = drawMarker(measureA_, QColor(80, 200, 120));
        const QPoint pb = drawMarker(measureB_, QColor(255, 180, 60));
        if (measureA_.has_value() && measureB_.has_value()) {
            painter.setPen(QPen(QColor(255, 220, 120), 2));
            painter.drawLine(pa, pb);
        }
    }
}

QPointF ViewportWidget::projectToScreen(const QVector3D& world) const {
    const QMatrix4x4 mvp = projectionMatrix() * viewMatrix();
    const QVector3D ndc = mvp.map(world);
    const double x = (static_cast<double>(ndc.x()) * 0.5 + 0.5) * width();
    const double y = (1.0 - (static_cast<double>(ndc.y()) * 0.5 + 0.5)) * height();
    return QPointF(x, y);
}

void ViewportWidget::updateFromCameraPosition(const QVector3D& pos, const QVector3D& target) {
    target_ = target;
    const QVector3D d = pos - target;
    distance_ = std::clamp(static_cast<double>(d.length()), 2.0, 20000.0);
    if (distance_ < 1e-6) {
        return;
    }
    const double pitch = std::asin(std::clamp(static_cast<double>(d.z()) / distance_, -1.0, 1.0));
    const double yaw = std::atan2(static_cast<double>(d.y()), static_cast<double>(d.x()));
    pitchDeg_ = pitch * 180.0 / 3.141592653589793;
    yawDeg_ = yaw * 180.0 / 3.141592653589793;
}

void ViewportWidget::setCameraFromSpherical() { update(); }

// --- Controls: Left=orbit, Middle=pan, Wheel=zoom, F=fit, R=reset ---

void ViewportWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    lastPos_ = event->pos();
    if (measureMode_ && (event->button() == Qt::LeftButton)) {
        pickVertex(event->pos());
        return;
    }
    if (event->button() == Qt::LeftButton) {
        orbiting_ = true;
    } else if (event->button() == Qt::MiddleButton ||
               (event->button() == Qt::RightButton)) {
        panning_ = true;
    }
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastPos_;
    lastPos_ = event->pos();
    if (orbiting_ && !measureMode_) {
        yawDeg_ += delta.x() * 0.4;
        pitchDeg_ = std::clamp(pitchDeg_ + delta.y() * 0.3, -89.0, 89.0);
        update();
        emit cameraChanged();
    } else if (panning_) {
        // Pan in camera plane, scaled by distance.
        const double yaw = yawDeg_ * 3.141592653589793 / 180.0;
        const double pitch = pitchDeg_ * 3.141592653589793 / 180.0;
        QVector3D viewDir(static_cast<float>(-std::cos(pitch) * std::cos(yaw)),
                          static_cast<float>(-std::cos(pitch) * std::sin(yaw)),
                          static_cast<float>(-std::sin(pitch)));
        QVector3D up(0, 0, 1);
        QVector3D right = QVector3D::crossProduct(viewDir, up);
        if (right.lengthSquared() < 1e-8F) {
            right = QVector3D(1, 0, 0);
        }
        right.normalize();
        QVector3D camUp = QVector3D::crossProduct(right, viewDir).normalized();
        const float scale = static_cast<float>(distance_ / height() * 1.2);
        target_ -= right * static_cast<float>(delta.x()) * scale;
        target_ += camUp * static_cast<float>(delta.y()) * scale;
        update();
        emit cameraChanged();
    }
    // Shift+Left also pans (trackpads without middle button).
    if ((event->modifiers() & Qt::ShiftModifier) && orbiting_) {
        orbiting_ = false;
        panning_ = true;
    }
}

void ViewportWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        orbiting_ = false;
    }
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        panning_ = false;
    }
}

void ViewportWidget::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    const double factor = std::pow(1.12, -steps);
    distance_ = std::clamp(distance_ * factor, 2.0, 20000.0);
    orthoHalf_ = std::clamp(orthoHalf_ * factor, 1.0, 20000.0);
    update();
    emit cameraChanged();
    event->accept();
}

void ViewportWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F) {
        fitModel();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_R) {
        resetCamera();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void ViewportWidget::pickVertex(const QPoint& clickPos) {
    if (positions_.size() < 3) {
        return;
    }
    // Project every vertex to screen; pick nearest within 25px.
    // Brute force is trivial for ~hundreds of vertices.
    double bestDist = 25.0;
    QVector3D best(0, 0, 0);
    bool found = false;
    for (size_t i = 0; i + 2 < positions_.size(); i += 3) {
        const QVector3D w(positions_[i], positions_[i + 1], positions_[i + 2]);
        const QPointF s = projectToScreen(w);
        const double dx = s.x() - clickPos.x();
        const double dy = s.y() - clickPos.y();
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < bestDist) {
            bestDist = d;
            best = w;
            found = true;
        }
    }
    if (!found) {
        return;
    }
    if (!measureA_.has_value() || (measureA_.has_value() && measureB_.has_value())) {
        measureA_ = best;
        measureB_.reset();
        emit measurementChanged(static_cast<double>(best.x()), static_cast<double>(best.y()),
                                static_cast<double>(best.z()), 0, 0, 0, false);
    } else {
        measureB_ = best;
        emit measurementChanged(
            static_cast<double>(measureA_->x()), static_cast<double>(measureA_->y()),
            static_cast<double>(measureA_->z()), static_cast<double>(best.x()),
            static_cast<double>(best.y()), static_cast<double>(best.z()), true);
    }
    update();
}

}  // namespace trinity::ui
