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

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core/version.hpp>
#endif

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
                } else if (cap.name == "pcb" && registry_->has("pcb")) {
                    engines::EngineRequest demo;
                    demo.engine = "pcb";
                    demo.operation = "create_board";
                    demo.parameters = core::Json{
                        {"width_mm", 50.0},
                        {"height_mm", 40.0},
                        {"components", {{{"part", "esp32"}}, {{"part", "regulator"}}}}};
                    const auto result = registry_->execute(demo);
                    if (result.success) {
                        entry.implemented = true;
                        entry.lastResult =
                            "50x40mm board: " +
                            std::to_string(result.result.value("component_count", 0)) +
                            " components";
                    }
                } else if (cap.name == "firmware" && registry_->has("firmware")) {
                    engines::EngineRequest demo;
                    demo.engine = "firmware";
                    demo.operation = "create_project";
                    demo.parameters = core::Json{{"name", "summary-demo"}};
                    const auto created = registry_->execute(demo);
                    if (created.success) {
                        engines::EngineRequest mcuReq;
                        mcuReq.engine = "firmware";
                        mcuReq.operation = "select_mcu";
                        mcuReq.parameters =
                            core::Json{{"project", created.result["project"]},
                                       {"mcu", "ESP32"}};
                        const auto withMcu = registry_->execute(mcuReq);
                        if (withMcu.success) {
                            entry.implemented = true;
                            entry.lastResult = "project created, mcu ESP32";
                        }
                    }
                } else if (cap.name == "vision" && registry_->has("vision")) {
                    // Real image ops exist only when the build has OpenCV;
                    // otherwise VisionEngine refuses and stays unimplemented.
#ifdef TRINITY_HAS_OPENCV
                    entry.implemented = true;
                    entry.lastResult = std::string("OpenCV ") + CV_VERSION +
                                       " image ops enabled";
#endif
                } else if (cap.name == "research" && registry_->has("research")) {
                    engines::EngineRequest indexReq;
                    indexReq.engine = "research";
                    indexReq.operation = "index_document";
                    indexReq.parameters = core::Json{
                        {"doc_id", "summary-demo"},
                        {"title", "Trinity research demo"},
                        {"text", "Deterministic local research index demo entry."}};
                    registry_->execute(indexReq);  // duplicate id on repeat calls is fine
                    engines::EngineRequest searchReq;
                    searchReq.engine = "research";
                    searchReq.operation = "search";
                    searchReq.parameters =
                        core::Json{{"query", "deterministic index"}, {"limit", 3}};
                    const auto found = registry_->execute(searchReq);
                    if (found.success && found.result.value("hit_count", 0) > 0) {
                        entry.implemented = true;
                        entry.lastResult = std::to_string(found.result.value("hit_count", 0)) +
                                           " hit(s) for 'deterministic index'";
                    }
                } else if (cap.name == "simulation" && registry_->has("simulation")) {
                    engines::EngineRequest demo;
                    demo.engine = "simulation";
                    demo.operation = "simulate_linear_motion";
                    demo.parameters = core::Json{
                        {"duration_s", 1.0},
                        {"initial_velocity_m_s", 2.0},
                        {"acceleration_m_s2", 1.0},
                        {"write_artifacts", false}};
                    const auto result = registry_->execute(demo);
                    if (result.success) {
                        entry.implemented = true;
                        entry.lastResult =
                            "linear_motion: " +
                            std::to_string(
                                result.result["result"].value("step_count", 0LL)) +
                            " steps, " +
                            result.result["result"].value("method", std::string("closed_form"));
                    }
                } else if (cap.name == "robotics" && registry_->has("robotics")) {
                    engines::EngineRequest fkReq;
                    fkReq.engine = "robotics";
                    fkReq.operation = "forward_kinematics";
                    fkReq.parameters =
                        core::Json{{"joint_angles", core::Json::array({0.0, 0.0})}};
                    const auto result = registry_->execute(fkReq);
                    if (result.success) {
                        const auto& pos = result.result["end_effector"]["position"];
                        entry.implemented = true;
                        entry.lastResult = "2-link FK: EE (" +
                                           std::to_string(pos.value("x", 0.0)) + ", " +
                                           std::to_string(pos.value("y", 0.0)) + ", " +
                                           std::to_string(pos.value("z", 0.0)) + ") m";
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
