// Trinity — ViewportController: camera, display modes, selection, stats.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "../../artifacts/Artifact.hpp"
#include "../../core/EventBus.hpp"
#include "../../rendering/3d/GeometryLoader.hpp"

namespace trinity::shell {

class ViewportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString viewPreset READ viewPreset WRITE setViewPreset NOTIFY viewPresetChanged)
    Q_PROPERTY(bool wireframe READ wireframe WRITE setWireframe NOTIFY displayChanged)
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY displayChanged)
    Q_PROPERTY(bool showAxes READ showAxes WRITE setShowAxes NOTIFY displayChanged)
    Q_PROPERTY(bool ortho READ ortho WRITE setOrtho NOTIFY displayChanged)
    Q_PROPERTY(bool spinning READ spinning WRITE setSpinning NOTIFY displayChanged)
    Q_PROPERTY(QString selectedArtifactId READ selectedArtifactId WRITE setSelectedArtifactId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap meshStats READ meshStats NOTIFY statsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statsChanged)

public:
    explicit ViewportController(core::EventBus* bus, artifacts::ArtifactStore* artifacts = nullptr, QObject* parent = nullptr);

    QString viewPreset() const { return viewPreset_; }
    bool wireframe() const { return wireframe_; }
    bool showGrid() const { return showGrid_; }
    bool showAxes() const { return showAxes_; }
    bool ortho() const { return ortho_; }
    bool spinning() const { return spinning_; }
    QString selectedArtifactId() const { return selectedArtifactId_; }
    QVariantMap meshStats() const { return meshStats_; }
    QString statusText() const { return statusText_; }

    void setViewPreset(const QString& v);
    void setWireframe(bool v);
    void setShowGrid(bool v);
    void setShowAxes(bool v);
    void setOrtho(bool v);
    void setSpinning(bool v);
    void setSelectedArtifactId(const QString& id);

    Q_INVOKABLE void setView(const QString& preset); // FRONT/TOP/RIGHT/ISO/FIT
    Q_INVOKABLE void fitView();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void refreshStats(const QString& artifactId = {});

signals:
    void viewPresetChanged();
    void displayChanged();
    void selectionChanged();
    void statsChanged();
    void viewAction(const QString& action);

private:
    QString viewPreset_ = "ISO";
    bool wireframe_ = false;
    bool showGrid_ = true;
    bool showAxes_ = true;
    bool ortho_ = false;
    bool spinning_ = false;
    QString selectedArtifactId_;
    QVariantMap meshStats_;
    QString statusText_ = "AWAITING GEOMETRY";
    core::EventBus* bus_ = nullptr;
    artifacts::ArtifactStore* artifacts_ = nullptr;
    rendering::LoadedGeometry lastGeometry_;
};

} // namespace trinity::shell
