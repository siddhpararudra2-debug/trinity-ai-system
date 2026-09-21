#pragma once

// ViewerController: CAD Engine -> Mesh -> Validation -> ArtifactManager ->
// Viewer bridge. Owns ViewerState, prefers the in-memory validated mesh
// rebuilt from the job result spec when available, else decodes the managed
// STL/spec artifact file. All heavy loads run off the GUI thread; results
// are delivered via queued signals so the UI stays responsive. The viewer
// never executes CAD generation.

#include <QObject>

#include <atomic>
#include <mutex>
#include <string>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/viewer/ArtifactLoader.hpp"
#include "trinity/viewer/ViewerState.hpp"

namespace trinity::jobs {
class JobManager;
}
namespace trinity::storage {
class ArtifactRepository;
}

namespace trinity::ui {

class ViewerController : public QObject {
    Q_OBJECT

public:
    explicit ViewerController(QObject* parent = nullptr);

    void setServices(jobs::JobManager* jobs, storage::ArtifactRepository* artifacts);

    const viewer::ViewerState& state() const;
    std::string currentJobId() const;
    std::string currentArtifactId() const;

public slots:
    // Open a managed artifact by id (validates integrity first).
    void openArtifact(const std::string& artifactId);
    // Open the best mesh for a job: rebuild from result spec when possible,
    // else load its first STL/spec artifact file.
    void openJob(const std::string& jobId);
    // Called from the MainWindow poll timer: auto-show the newest completed
    // CAD generate job. No-op when nothing new.
    void pollForNewCadJob();
    void reloadCurrent();
    void clear();

    // UI state sync (keeps ViewerState truthful for tests/inspector).
    void setCamera(const viewer::Camera& cam);
    void setProjection(viewer::ProjectionMode mode);
    void setRenderMode(viewer::RenderMode mode);
    void setGrid(bool on);
    void setAxes(bool on);
    void updateMeasurement(double ax, double ay, double az, double bx, double by,
                           double bz, bool complete);
    void clearMeasurement();

signals:
    void stateChanged();
    void loadingChanged(bool loading);
    void loadError(const QString& message);
    void modelReady(const QString& jobId);

private:
    void setLoading(bool on);
    void fail(const std::string& message);
    void deliver(viewer::LoadedModel loaded, std::string jobId, std::string artifactId);

    jobs::JobManager* jobs_ = nullptr;
    storage::ArtifactRepository* artifacts_ = nullptr;

    mutable std::mutex mutex_;
    viewer::ViewerState state_;
    std::string currentJobId_;
    std::string currentArtifactId_;
    std::atomic<std::uint64_t> generation_{0};
};

}  // namespace trinity::ui
