// Trinity — Qt Quick desktop shell entry point (built only with
// TRINITY_WITH_QT=ON). Creates the same core objects as the console shell and
// exposes them to QML through a TrinityContext bridge object.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "../artifacts/Artifact.hpp"
#include "../commands/Commands.hpp"
#include "../core/Logging.hpp"
#include "../core/Paths.hpp"
#include "../db/Schema.hpp"
#include "../engines/Engine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../projects/Project.hpp"
#include "../settings/Settings.hpp"
#include "../validation/ValidationEngine.hpp"
#include "TrinityBridge.hpp"

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName("Trinity");
    QGuiApplication::setApplicationName("Trinity");
    QGuiApplication::setApplicationVersion("0.1.0");

    // High-DPI is default-on in Qt 6; window icon and persistence below.
    trinity::core::Paths::ensure_layout();
    trinity::core::Logger::instance().open_file_sink(trinity::core::Paths::main_log_file());

    auto db_result = trinity::db::open_and_migrate(trinity::core::Paths::database_file());
    if (db_result.is_error()) {
        qCritical("database unavailable: %s", db_result.error().message().c_str());
        return 1;
    }
    auto db = std::make_unique<trinity::db::Database>(db_result.take_value());

    trinity::engines::bootstrap_builtin_engines();

    // The bridge owns the core graph and outlives the QML engine.
    auto* bridge = new trinity::shell::TrinityBridge(std::move(db), &app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Trinity", bridge);
    engine.loadFromModule("Trinity.Shell", "Main");
    if (engine.rootObjects().isEmpty()) return 2;

    const int code = app.exec();
    delete bridge;
    return code;
}
