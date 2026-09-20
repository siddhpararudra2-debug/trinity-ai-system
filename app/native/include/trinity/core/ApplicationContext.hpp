#pragma once

// Central application/core context. Owns configuration, logger,
// database, filesystem and core services so main.cpp stays thin.
// All SQL/database code stays inside the storage layer; this context
// only wires already-built services together.

#include <memory>
#include <string>
#include <vector>

#include "../artifacts/Artifact.hpp"
#include "../core/Config.hpp"
#include "../core/Logger.hpp"
#include "../core/Result.hpp"
#include "../engines/EngineRegistry.hpp"
#include "../fs/Filesystem.hpp"
#include "../intelligence/IModelProvider.hpp"
#include "../jobs/Job.hpp"
#include "../storage/Database.hpp"
#include "../storage/Repositories.hpp"

namespace trinity::core {

struct EngineListEntry {
    std::string name;
    std::string version;
    std::vector<std::string> capabilities;
    bool implemented = false;  // true when the engine can do real work
    std::string lastResult;    // short demo result, e.g. "2+3*4=14"
};

struct InitSummary {
    std::string dbPath;
    size_t engineCount = 0;
    std::vector<EngineListEntry> engines;
    std::string modelProvider;
    bool coreOk = false;
    std::string error;
};

class ApplicationContext {
public:
    static ApplicationContext& instance();

    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    /// Full startup: config -> logger(file) -> dirs -> database -> services.
    /// Idempotent; safe to call twice (second call is a no-op when ready).
    Status initialize();
    void shutdown();

    bool isReady() const noexcept { return ready_; }

    Settings& config() { return settings_; }
    const Settings& config() const { return settings_; }
    storage::Database& db() { return *db_; }
    fs::Filesystem& filesystem() { return fs_; }
    engines::EngineRegistry& engines() { return *registry_; }
    artifacts::ArtifactManager& artifacts() { return *artifacts_; }
    jobs::JobManager& jobs() { return *jobs_; }
    intelligence::IModelProvider& model() { return *model_; }
    storage::JobRepository& jobRepository() { return *jobRepo_; }
    storage::WorkflowRepository& workflowRepository() { return *workflowRepo_; }
    storage::ArtifactRepository& artifactRepository() { return *artifactRepo_; }

    InitSummary summary() const;

private:
    ApplicationContext() = default;

    bool ready_ = false;
    Settings settings_;
    fs::Filesystem fs_;
    std::shared_ptr<storage::Database> db_;
    std::shared_ptr<engines::EngineRegistry> registry_;
    std::shared_ptr<artifacts::ArtifactManager> artifacts_;
    std::shared_ptr<jobs::JobManager> jobs_;
    std::shared_ptr<intelligence::IModelProvider> model_;
    std::shared_ptr<storage::JobRepository> jobRepo_;
    std::shared_ptr<storage::WorkflowRepository> workflowRepo_;
    std::shared_ptr<storage::ArtifactRepository> artifactRepo_;
    std::string initError_;
};

}  // namespace trinity::core
