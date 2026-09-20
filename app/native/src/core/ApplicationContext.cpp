#include "trinity/core/ApplicationContext.hpp"

#include <filesystem>

#include "trinity/core/Paths.hpp"
#include "trinity/engines/StubEngines.hpp"

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
        model_ = std::make_shared<intelligence::NullModelProvider>();
        jobRepo_ = std::make_shared<storage::JobRepository>(db_);
        workflowRepo_ = std::make_shared<storage::WorkflowRepository>(db_);
        artifactRepo_ = std::make_shared<storage::ArtifactRepository>(db_);

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
            // Only math does real deterministic work in this phase; cad
            // exposes describe only; the rest are scaffolded stubs.
            entry.implemented = (cap.name == "math");
            out.engines.push_back(std::move(entry));
        }
    }
    out.modelProvider = model_ ? model_->info().displayName : std::string("none");
    out.coreOk = ready_ && initError_.empty();
    out.error = initError_;
    return out;
}

}  // namespace trinity::core
