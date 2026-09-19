// Trinity application entry point.
//
// Startup order (mirrors the Python lifespan in src/main.py):
//   configuration -> logging -> paths -> SQLite init -> engine registry
//   -> model provider (Null) -> Qt window -> clean shutdown.
//
// --selftest runs the same bootstrap headlessly (no QApplication) and
// exits 0 on success so CI and operators can verify startup without
// a display server.

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/core/Config.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Paths.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/intelligence/IModelProvider.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/storage/Database.hpp"

#ifdef TRINITY_WITH_QT
#include <QApplication>

#include "../ui/MainWindow.hpp"
#endif

namespace {

struct Bootstrap {
    trinity::core::Settings settings;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;
    std::shared_ptr<trinity::intelligence::IModelProvider> model;
    bool ok = false;
    std::string error;
};

Bootstrap bootstrap() {
    Bootstrap boot;
    auto& log = trinity::core::Logger::instance();
    try {
        boot.settings = trinity::core::loadSettings();
        trinity::core::ensureStorageLayout(boot.settings);

        boot.db = std::make_shared<trinity::storage::Database>(boot.settings.dbPath);
        boot.db->init();

        boot.registry = std::make_shared<trinity::engines::EngineRegistry>();
        // Later phases register math/cad/pcb/firmware/vision/research/
        // simulation/robotics here. Nothing ships yet by design.

        boot.artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(
            boot.db, boot.settings.artifactsDir);
        boot.jobs = std::make_shared<trinity::jobs::JobManager>(boot.db, boot.registry,
                                                                boot.artifacts);
        boot.model = std::make_shared<trinity::intelligence::NullModelProvider>();

        log.info("main", "trinity started",
                 trinity::core::Json{{"engines", boot.registry->list().size()}});
        boot.ok = true;
    } catch (const std::exception& exc) {
        boot.ok = false;
        boot.error = exc.what();
        log.error("main", "trinity startup failed",
                  trinity::core::Json{{"error", boot.error}});
    }
    return boot;
}

int runSelftest() {
#ifdef _WIN32
    // Headless console output for a WIN32-subsystem binary.
    AttachConsole(ATTACH_PARENT_PROCESS);
    FILE* ignored = nullptr;
    freopen_s(&ignored, "CONOUT$", "w", stdout);
    freopen_s(&ignored, "CONOUT$", "w", stderr);
#endif
    Bootstrap boot = bootstrap();
    std::vector<std::pair<std::string, bool>> checks = {
        {"config_loaded", !boot.settings.storageRoot.empty()},
        {"database_initialized", boot.ok},
        {"registry_ready", boot.registry != nullptr},
        {"model_reports_unavailable",
         boot.model != nullptr && !boot.model->info().available},
        {"planned_engines_known",
         trinity::engines::EngineRegistry::plannedEngineNames().size() == 8},
    };
    bool allOk = boot.ok;
    for (const auto& [name, passed] : checks) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
        allOk = allOk && passed;
    }
    // Engine lookup must fail truthfully while no engines are registered.
    bool missOk = false;
    try {
        boot.registry->get("math");
    } catch (const trinity::core::EngineNotFoundError&) {
        missOk = true;
    }
    std::cout << (missOk ? "[PASS] " : "[FAIL] ") << "missing_engine_raises\n";
    return (allOk && missOk) ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest") {
            return runSelftest();
        }
    }

#ifdef TRINITY_WITH_QT
    Bootstrap boot = bootstrap();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Trinity"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    trinity::ui::InitSummary summary;
    summary.dbPath = boot.settings.dbPath;
    summary.engineCount = boot.registry ? boot.registry->list().size() : 0;
    summary.modelProvider =
        boot.model ? boot.model->info().displayName : std::string("none");
    summary.coreOk = boot.ok;

    trinity::ui::MainWindow window(summary);
    window.show();
    const int code = app.exec();

    trinity::core::Logger::instance().info("main", "trinity shutdown",
                                           trinity::core::Json{{"code", code}});
    return code;
#else
    std::cerr << "Trinity built without Qt; run with --selftest.\n";
    return runSelftest() == 0 ? 0 : 1;
#endif
}
