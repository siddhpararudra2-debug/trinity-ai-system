#include "ViewerController.hpp"

#include <QMetaObject>
#include <thread>

#include "trinity/core/Logger.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/validation/ValidationResult.hpp"
#include "trinity/viewer/ArtifactLoader.hpp"

namespace trinity::ui {

ViewerController::ViewerController(QObject* parent) : QObject(parent) {}

void ViewerController::setServices(jobs::JobManager* jobs,
                                   storage::ArtifactRepository* artifacts) {
    jobs_ = jobs;
    artifacts_ = artifacts;
}

const viewer::ViewerState& ViewerController::state() const { return state_; }

std::string ViewerController::currentJobId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentJobId_;
}

std::string ViewerController::currentArtifactId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentArtifactId_;
}

void ViewerController::setLoading(bool on) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setLoading(on);
        if (on) {
            state_.clearError();
        }
    }
    emit loadingChanged(on);
    emit stateChanged();
}

void ViewerController::fail(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setLoading(false);
        state_.setError(message);
    }
    core::Logger::instance().warning("viewer", "model load failed",
                                     core::Json{{"error", message}});
    emit loadingChanged(false);
    emit loadError(QString::fromStdString(message));
    emit stateChanged();
}

void ViewerController::deliver(viewer::LoadedModel loaded, std::string jobId,
                               std::string artifactId) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setLoading(false);
        state_.setMesh(std::move(loaded.mesh), std::move(loaded.artifact),
                       std::move(loaded.validation));
        if (!jobId.empty()) {
            // Prefer the job id that produced this geometry.
            if (state_.validation().jobId.empty()) {
                auto v = state_.validation();
                v.jobId = jobId;
                state_.setValidation(std::move(v));
            }
        }
        currentJobId_ = std::move(jobId);
        currentArtifactId_ = std::move(artifactId);
    }
    emit loadingChanged(false);
    emit modelReady(QString::fromStdString(currentJobId_));
    emit stateChanged();
}

void ViewerController::openArtifact(const std::string& artifactId) {
    if (artifacts_ == nullptr) {
        fail("Artifact service unavailable");
        return;
    }
    if (artifactId.empty()) {
        fail("No artifact selected");
        return;
    }
    const std::uint64_t gen = ++generation_;
    setLoading(true);
    auto* repo = artifacts_;
    // Off the GUI thread: file IO + STL decode + validation.
    std::thread([this, repo, artifactId, gen]() {
        try {
            artifacts::Artifact artifact = repo->get(artifactId);
            viewer::LoadedModel loaded =
                viewer::loadModelFromArtifactFile(artifact);
            if (gen != generation_.load()) {
                return;  // superseded by a newer request
            }
            QMetaObject::invokeMethod(
                this,
                [this, loaded = std::move(loaded), artifactId]() mutable {
                    const std::string jobOfLoaded = loaded.artifact.jobId;
                    deliver(std::move(loaded), jobOfLoaded, artifactId);
                },
                Qt::QueuedConnection);
        } catch (const std::exception& exc) {
            if (gen != generation_.load()) {
                return;
            }
            const std::string msg = exc.what();
            QMetaObject::invokeMethod(
                this, [this, msg]() { fail(msg); }, Qt::QueuedConnection);
        } catch (...) {
            if (gen != generation_.load()) {
                return;
            }
            QMetaObject::invokeMethod(
                this, [this]() { fail("Artifact load failed (unknown error)"); },
                Qt::QueuedConnection);
        }
    }).detach();
}

void ViewerController::openJob(const std::string& jobId) {
    if (jobs_ == nullptr || artifacts_ == nullptr) {
        fail("Job service unavailable");
        return;
    }
    if (jobId.empty()) {
        fail("No job selected");
        return;
    }
    const std::uint64_t gen = ++generation_;
    setLoading(true);
    auto* jobs = jobs_;
    auto* repo = artifacts_;
    std::thread([this, jobs, repo, jobId, gen]() {
        try {
            jobs::Job job = jobs->get(jobId);
            if (job.status != jobs::JobStatus::Completed) {
                throw core::RequestValidationError(
                    "Job '" + jobId.substr(0, 8) + "' is not completed (" +
                        jobs::toString(job.status) + "); no viewable geometry yet",
                    {}, "viewer");
            }
            // Preferred path: rebuild the exact validated mesh from the
            // in-memory job result spec (no file IO, always exact).
            bool rebuilt = false;
            viewer::LoadedModel loaded;
            try {
                if (!job.result.is_null() && job.result.is_object() &&
                    job.result.contains("spec")) {
                    loaded.mesh = viewer::rebuildMeshFromJobResult(job.result);
                    loaded.source = "memory";
                    rebuilt = true;
                }
            } catch (...) {
                rebuilt = false;  // fall through to file path
            }
            std::string artifactId;
            artifacts::Artifact meta;
            // Attach the first STL/spec artifact for metadata + validation.
            try {
                const auto list = repo->listForJob(jobId);
                for (const auto& a : list) {
                    if (viewer::isSupportedMeshExtension(a.path) ||
                        viewer::isSpecJsonExtension(a.path)) {
                        meta = a;
                        artifactId = a.artifactId;
                        break;
                    }
                }
                if (artifactId.empty() && !list.empty()) {
                    meta = list.front();
                    artifactId = meta.artifactId;
                }
            } catch (...) {
            }
            if (rebuilt) {
                loaded.artifact = meta;
                loaded.artifact.jobId = jobId;
                validation::ValidationResult v;
                v.jobId = jobId;
                v.operation = job.operation;
                v.status = validation::ValidationStatus::Validated;
                v.message = "Generated geometry passed all checks";
                loaded.validation = std::move(v);
                loaded.jobResult = job.result;
            } else {
                if (artifactId.empty()) {
                    // No usable artifact: surface job error when present.
                    std::string detail;
                    if (!job.error.is_null()) {
                        detail = job.error.dump();
                    }
                    throw core::ArtifactNotFoundError(
                        "Job has no viewable STL/spec artifact" +
                            (detail.empty() ? "" : ": " + detail),
                        {}, "viewer");
                }
                artifacts::Artifact artifact = repo->get(artifactId);
                loaded = viewer::loadModelFromArtifactFile(artifact);
            }
            if (gen != generation_.load()) {
                return;
            }
            QMetaObject::invokeMethod(
                this,
                [this, loaded = std::move(loaded), jobId, artifactId]() mutable {
                    deliver(std::move(loaded), jobId, artifactId);
                },
                Qt::QueuedConnection);
        } catch (const std::exception& exc) {
            if (gen != generation_.load()) {
                return;
            }
            const std::string msg = exc.what();
            QMetaObject::invokeMethod(
                this, [this, msg]() { fail(msg); }, Qt::QueuedConnection);
        } catch (...) {
            if (gen != generation_.load()) {
                return;
            }
            QMetaObject::invokeMethod(
                this, [this]() { fail("Job load failed (unknown error)"); },
                Qt::QueuedConnection);
        }
    }).detach();
}

void ViewerController::pollForNewCadJob() {
    if (jobs_ == nullptr || artifacts_ == nullptr) {
        return;  // services not wired yet (e.g. MainWindow still constructing)
    }
    // Already showing something and not in error: only auto-advance to a
    // *newer* completed CAD job. Never steal a user-selected artifact.
    std::string current;
    bool hasMesh = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current = currentJobId_;
        hasMesh = state_.hasMesh();
        if (state_.loading()) {
            return;
        }
    }
    std::vector<jobs::Job> jobs;
    try {
        jobs = jobs_->listRecent(20);
    } catch (...) {
        return;
    }
    for (const auto& job : jobs) {
        const bool isCad =
            (job.engine == "cad") &&
            (job.operation == "generate" || job.operation == "generate_quadcopter_frame");
        if (!isCad || job.status != jobs::JobStatus::Completed) {
            continue;
        }
        if (job.jobId == current) {
            return;  // already showing the newest
        }
        // If user picked an artifact manually and it still exists, don't
        // auto-switch unless this job is strictly newer (listRecent is
        // newest-first, so the first match is the newest).
        if (hasMesh && !current.empty()) {
            // Only auto-switch when current job is no longer the newest.
            openJob(job.jobId);
            return;
        }
        openJob(job.jobId);
        return;
    }
}

void ViewerController::reloadCurrent() {
    std::string job, artifact;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = currentJobId_;
        artifact = currentArtifactId_;
    }
    if (!artifact.empty()) {
        openArtifact(artifact);
    } else if (!job.empty()) {
        openJob(job);
    }
}

void ViewerController::clear() {
    ++generation_;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.clearMesh();
        state_.setLoading(false);
        state_.clearError();
        currentJobId_.clear();
        currentArtifactId_.clear();
    }
    emit stateChanged();
}

void ViewerController::setCamera(const viewer::Camera& cam) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.camera() = cam;
    }
    emit stateChanged();
}

void ViewerController::setProjection(viewer::ProjectionMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setProjection(mode);
    }
    emit stateChanged();
}

void ViewerController::setRenderMode(viewer::RenderMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setRenderMode(mode);
    }
    emit stateChanged();
}

void ViewerController::setGrid(bool on) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setGrid(on);
    }
    emit stateChanged();
}

void ViewerController::setAxes(bool on) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.setAxes(on);
    }
    emit stateChanged();
}

void ViewerController::updateMeasurement(double ax, double ay, double az, double bx,
                                         double by, double bz, bool complete) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& m = state_.measurement();
        m.a = {ax, ay, az};
        m.b = {bx, by, bz};
        m.hasA = true;
        m.hasB = complete;
        if (!complete) {
            // First point only: keep B cleared until second pick.
            m.hasB = false;
        }
    }
    emit stateChanged();
}

void ViewerController::clearMeasurement() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.measurement().clear();
    }
    emit stateChanged();
}

}  // namespace trinity::ui
