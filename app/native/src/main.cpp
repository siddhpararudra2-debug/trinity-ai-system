// Trinity application entry point (thin shell over ApplicationContext).
//
// Startup order (mirrors the Python lifespan in src/main.py):
//   configuration -> logging -> paths -> SQLite init -> engine registry
//   -> model provider (Null) -> Qt window -> clean shutdown.
//
// --selftest runs the same bootstrap headlessly (no QApplication) and
// exits 0 on success so CI and operators can verify startup without
// a display server.

#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "trinity/core/ApplicationContext.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/engines/EngineRegistry.hpp"

#ifdef TRINITY_WITH_QT
#include <QApplication>

#include "../ui/MainWindow.hpp"
#endif

namespace {

int runSelftest() {
#ifdef _WIN32
    // Headless console output for a WIN32-subsystem binary.
    AttachConsole(ATTACH_PARENT_PROCESS);
    FILE* ignored = nullptr;
    freopen_s(&ignored, "CONOUT$", "w", stdout);
    freopen_s(&ignored, "CONOUT$", "w", stderr);
#endif
    auto& context = trinity::core::ApplicationContext::instance();
    const auto status = context.initialize();
    const auto summary = context.summary();
    const size_t engineCount = context.engines().list().size();
    std::vector<std::pair<std::string, bool>> checks = {
        {"config_loaded", !context.config().storageRoot.empty()},
        {"database_initialized", status.isOk()},
        {"registry_ready", engineCount == 8},
        {"model_reports_unavailable", !context.model().info().available},
        {"planned_engines_known",
         trinity::engines::EngineRegistry::plannedEngineNames().size() == 8},
        {"log_file_configured", !context.config().logFilePath().empty()},
    };
    bool allOk = status.isOk();
    for (const auto& [name, passed] : checks) {
        std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
        allOk = allOk && passed;
    }
    // Unknown engines must fail truthfully with structured errors.
    bool missOk = false;
    try {
        context.engines().get("ghost-engine-xyz");
    } catch (const trinity::core::EngineNotFoundError&) {
        missOk = true;
    }
    std::cout << (missOk ? "[PASS] " : "[FAIL] ") << "missing_engine_raises\n";
    // MathEngine must evaluate deterministically (2 + 3 * 4 = 14).
    bool mathOk = false;
    try {
        trinity::engines::EngineRequest mathReq;
        mathReq.engine = "math";
        mathReq.operation = "evaluate_expression";
        mathReq.parameters = trinity::core::Json{{"expression", "2 + 3 * 4"}};
        const auto mathResult = context.engines().execute(mathReq);
        mathOk = mathResult.success &&
                 mathResult.result.value("value", 0.0) == 14.0 &&
                 mathResult.validation.has_value() && mathResult.validation->passed();
    } catch (...) {
        mathOk = false;
    }
    std::cout << (mathOk ? "[PASS] " : "[FAIL] ") << "math_evaluates_expression\n";
    // Unsupported operations must refuse truthfully, never fake results.
    bool refuseOk = false;
    try {
        trinity::engines::EngineRequest cadReq;
        cadReq.engine = "cad";
        cadReq.operation = "generate_quadcopter_frame";
        const auto cadResult = context.engines().execute(cadReq);
        refuseOk = !cadResult.success && !cadResult.errors.empty();
    } catch (const trinity::core::CapabilityUnavailableError&) {
        refuseOk = true;
    } catch (...) {
        refuseOk = false;
    }
    std::cout << (refuseOk ? "[PASS] " : "[FAIL] ") << "unsupported_operation_refuses\n";
    // Model seam must refuse truthfully without an LLM.
    trinity::intelligence::ModelRequest request;
    request.prompt = "selftest";
    const auto response = context.model().generate(request);
    const bool modelOk = !response.success && !response.error.is_null();
    std::cout << (modelOk ? "[PASS] " : "[FAIL] ") << "model_refuses_without_llm\n";
    allOk = allOk && missOk && mathOk && refuseOk && modelOk;
    (void)summary;
    context.shutdown();
    return allOk ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest") {
            return runSelftest();
        }
    }

#ifdef TRINITY_WITH_QT
    auto& context = trinity::core::ApplicationContext::instance();
    const auto status = context.initialize();
    const auto summary = context.summary();

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Trinity"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    trinity::ui::InitSummary uiSummary;
    uiSummary.dbPath = summary.dbPath;
    uiSummary.engineCount = summary.engineCount;
    for (const auto& entry : summary.engines) {
        trinity::ui::EngineEntry out;
        out.name = entry.name;
        out.version = entry.version;
        out.implemented = entry.implemented;
        uiSummary.engines.push_back(std::move(out));
    }
    uiSummary.modelProvider = summary.modelProvider;
    uiSummary.coreOk = status.isOk();

    trinity::ui::MainWindow window(uiSummary);
    window.show();
    const int code = app.exec();

    context.shutdown();
    return code;
#else
    std::cerr << "Trinity built without Qt; run with --selftest.\n";
    return runSelftest() == 0 ? 0 : 1;
#endif
}
