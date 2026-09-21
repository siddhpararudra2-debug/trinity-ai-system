#include "trinity/core/ApplicationContext.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/cad/Mesh.hpp"
#include "trinity/cad/Validators.hpp"
#include "trinity/core/Paths.hpp"
#include "trinity/engines/StubEngines.hpp"
#include "trinity/intelligence/ModelProviderFactory.hpp"

namespace trinity::core {

ApplicationContext& ApplicationContext::instance() {
    static ApplicationContext context;
    return context;
}

Status ApplicationContext::initialize() {
    if (ready_) {
        return okStatus();
    }
    auto& log = Logger::instance();
    try {
        settings_ = loadSettings();
        log.setLogFile(settings_.logFilePath());
        log.info("app", "trinity startup", settings_.toJson());
        log.info("app", "configuration loaded",
                 Json{{"mode", settings_.buildMode},
                      {"storage_root", settings_.storageRoot},
                      {"db_path", settings_.dbPath}});

        ensureStorageLayout(settings_);
        log.info("app", "storage layout ensured",
                 Json{{"storage_root", settings_.storageRoot}});

        db_ = std::make_shared<storage::Database>(settings_.dbPath);
        db_->init();
        log.info("app", "database initialized", Json{{"db_path", settings_.dbPath}});

        registry_ = std::make_shared<engines::EngineRegistry>();
        // Application -> Core init -> Engine Registry -> Register engines -> UI.
        engines::registerAllEngines(*registry_);
        {
            core::Json names = core::Json::array();
            for (const auto& cap : registry_->list()) {
                names.push_back(cap.name);
            }
            log.info("app", "engine registry ready",
                     Json{{"engines", registry_->list().size()}, {"names", names}});
        }

        artifacts_ = std::make_shared<artifacts::ArtifactManager>(db_,
                                                                  settings_.artifactsDir);
        jobs_ = std::make_shared<jobs::JobManager>(db_, registry_, artifacts_);
        jobRepo_ = std::make_shared<storage::JobRepository>(db_);
        workflowRepo_ = std::make_shared<storage::WorkflowRepository>(db_);
        artifactRepo_ = std::make_shared<storage::ArtifactRepository>(db_);
        executor_ = std::make_shared<workflows::WorkflowExecutor>(jobs_, registry_, workflowRepo_);
        pipeline_ = std::make_shared<intelligence::RequestPipeline>(jobs_, registry_);
        worker_ = std::make_shared<jobs::JobWorker>(jobs_, 1);
        worker_->start();
        pipeline_->setWorker(worker_);
        // Recover interrupted jobs (QUEUED/RUNNING) after migrations.
        try {
            const int recovered = jobs_->recoverOnStartup();
            (void)recovered;
        } catch (const std::exception& exc) {
            log.warning("app", "job recovery failed",
                        Json{{"error", exc.what()}});
        }
        // Provider selection: TRINITY_MODEL_PROVIDER id via the factory.
        // Unknown ids fall back to Null so a typo never prevents boot.
        // Secrets are never logged — only the provider id is recorded.
        try {
            model_ = intelligence::ModelProviderFactory::instance().createOrNull(
                settings_.model.providerId, settings_.model.toJson());
            if (settings_.model.providerId != "null" &&
                model_->info().providerId == "null") {
                log.warning("model", "requested provider unavailable; using null provider",
                            core::Json{{"requested", settings_.model.providerId}});
            }
            const auto configStatus = model_->configure(settings_.model.toJson());
            if (!configStatus.isOk()) {
                log.warning("model", "provider configure reported failure",
                            core::Json{{"provider", settings_.model.providerId}});
            }
        } catch (const std::exception& exc) {
            log.warning("model", "provider selection failed; using null provider",
                        core::Json{{"provider", settings_.model.providerId},
                                   {"error", exc.what()}});
            model_ = std::make_shared<intelligence::NullModelProvider>();
        }
        log.info("engine", "engine operations ready", Json::object());
        log.info("model", "model provider ready",
                 Json{{"provider", model_->info().displayName},
                      {"available", model_->info().available}});

        ready_ = true;
        log.info("app", "trinity started",
                 Json{{"engines", registry_->list().size()}});
        return okStatus();
    } catch (const TrinityError& exc) {
        initError_ = exc.what();
        log.error("app", "trinity startup failed", exc.toJson());
        return Status::fail(exc.info());
    } catch (const std::exception& exc) {
        initError_ = exc.what();
        log.error("app", "trinity startup failed",
                  Json{{"error", initError_}});
        return errStatus(ErrorCode::TrinityError, initError_, Json::object(), "app");
    }
}

void ApplicationContext::shutdown() {
    if (!ready_) {
        return;
    }
    Logger::instance().info("app", "trinity shutdown", Json::object());
    if (worker_) {
        worker_->stop();
    }
    worker_.reset();
    pipeline_.reset();
    executor_.reset();
    jobs_.reset();
    artifacts_.reset();
    registry_.reset();
    model_.reset();
    jobRepo_.reset();
    workflowRepo_.reset();
    artifactRepo_.reset();
    db_.reset();
    ready_ = false;
}

InitSummary ApplicationContext::summary() const {
    InitSummary out;
    out.dbPath = settings_.dbPath;
    out.engineCount = registry_ ? registry_->list().size() : 0;
    if (registry_) {
        for (const auto& cap : registry_->list()) {
            EngineListEntry entry;
            entry.name = cap.name;
            entry.version = cap.version;
            entry.capabilities = cap.capabilities;
            // In-memory demos only: no files, no DB writes. Failures leave
            // lastResult empty rather than breaking startup.
            try {
                if (cap.name == "math" && registry_->has("math")) {
                    engines::EngineRequest demo;
                    demo.engine = "math";
                    demo.operation = "evaluate_expression";
                    demo.parameters = core::Json{{"expression", "2 + 3 * 4"}};
                    const auto result = registry_->execute(demo);
                    if (result.success) {
                        char buffer[32];
                        std::snprintf(buffer, sizeof(buffer), "%.6g",
                                      result.result.value("value", 0.0));
                        entry.implemented = true;
                        entry.lastResult = std::string("2+3*4=") + buffer;
                    }
                } else if (cap.name == "cad") {
                    const cad::FrameParams params =
                        cad::FrameParams::fromRequest(core::Json::object());
                    const cad::Mesh mesh = cad::buildQuadcopterFrame(params);
                    const cad::FrameValidation check =
                        cad::validateQuadcopterFrame(params, mesh);
                    if (check.ok) {
                        entry.implemented = true;
                        entry.lastResult =
                            "50mm frame: " + std::to_string(mesh.triangleCount()) +
                            " triangles, span " +
                            std::to_string(static_cast<int>(std::round(
                                check.checks.value("expected_span_mm", 0.0)))) +
                            "mm";
                    }
                }
            } catch (...) {
            }
            out.engines.push_back(std::move(entry));
        }
    }
    out.modelProvider = model_ ? model_->info().displayName : std::string("none");
    out.coreOk = ready_ && initError_.empty();
    out.error = initError_;
    return out;
}

}  // namespace trinity::core
