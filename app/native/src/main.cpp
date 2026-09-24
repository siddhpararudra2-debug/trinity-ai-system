// Trinity application entry point (thin shell over ApplicationContext).
//
// Startup order (mirrors the Python lifespan in src/main.py):
//   configuration -> logging -> paths -> SQLite init -> engine registry
//   -> model provider (Null) -> Qt window -> clean shutdown.
//
// --selftest runs the same bootstrap headlessly (no QApplication) and
// exits 0 on success so CI and operators can verify startup without
// a display server.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

#include "trinity/core/ApplicationContext.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/intelligence/ModelProviderFactory.hpp"
#include "trinity/intelligence/Planner.hpp"

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
    // CAD must generate a real 108-triangle 50 mm frame. The "none"
    // output requests geometry without scratch files, so the headless
    // check leaves no temp litter behind.
    bool cadOk = false;
    try {
        trinity::engines::EngineRequest genReq;
        genReq.engine = "cad";
        genReq.operation = "generate";
        genReq.parameters = trinity::core::Json{
            {"type", "quadcopter_frame"},
            {"parameters", {{"overall_size", 50}}},
            {"outputs", {"none"}}};
        const auto genResult = context.engines().execute(genReq);
        cadOk = genResult.success &&
                genResult.result.value("triangle_count", 0) == 108 &&
                genResult.validation.has_value() && genResult.validation->passed();
    } catch (...) {
        cadOk = false;
    }
    std::cout << (cadOk ? "[PASS] " : "[FAIL] ") << "cad_generates_frame\n";
    // PCB must create a real board: 50x40 mm with two starter parts.
    bool pcbOk = false;
    try {
        trinity::engines::EngineRequest pcbReq;
        pcbReq.engine = "pcb";
        pcbReq.operation = "create_board";
        pcbReq.parameters = trinity::core::Json{
            {"width_mm", 50.0},
            {"height_mm", 40.0},
            {"components", {{{"part", "esp32"}}, {{"part", "regulator"}}}}};
        const auto pcbResult = context.engines().execute(pcbReq);
        pcbOk = pcbResult.success &&
                pcbResult.result.value("component_count", 0) == 2 &&
                pcbResult.validation.has_value() && pcbResult.validation->passed();
    } catch (...) {
        pcbOk = false;
    }
    std::cout << (pcbOk ? "[PASS] " : "[FAIL] ") << "pcb_creates_board\n";
    // FirmwareEngine must generate deterministic sources for a simple GPIO project.
    bool fwOk = false;
    try {
        trinity::engines::EngineRequest createReq;
        createReq.engine = "firmware";
        createReq.operation = "create_project";
        createReq.parameters = trinity::core::Json{{"name", "selftest-fw"}};
        const auto created = context.engines().execute(createReq);
        if (created.success) {
            trinity::engines::EngineRequest mcuReq;
            mcuReq.engine = "firmware";
            mcuReq.operation = "select_mcu";
            mcuReq.parameters = trinity::core::Json{
                {"project", created.result["project"]}, {"mcu", "ESP32"}};
            const auto withMcu = context.engines().execute(mcuReq);
            if (withMcu.success) {
                trinity::engines::EngineRequest pinReq;
                pinReq.engine = "firmware";
                pinReq.operation = "configure_pin";
                pinReq.parameters = trinity::core::Json{
                    {"project", withMcu.result["project"]},
                    {"pin", "GPIO2"},
                    {"function", "status_led"},
                    {"direction", "out"}};
                const auto withPin = context.engines().execute(pinReq);
                if (withPin.success) {
                    trinity::engines::EngineRequest genReq;
                    genReq.engine = "firmware";
                    genReq.operation = "generate_firmware";
                    genReq.parameters = trinity::core::Json{
                        {"project", withPin.result["project"]}};
                    const auto generated = context.engines().execute(genReq);
                    fwOk = generated.success &&
                           generated.result.value("file_count", 0) == 3;
                }
            }
        }
    } catch (...) {
        fwOk = false;
    }
    std::cout << (fwOk ? "[PASS] " : "[FAIL] ") << "firmware_generates_sources\n";
    // Vision must load a real image when OpenCV is present, and refuse
    // truthfully when it is not. Both paths prove no fabricated results.
    bool visionOk = false;
    try {
        trinity::engines::EngineRequest visionReq;
        visionReq.engine = "vision";
        visionReq.operation = "load_image";
#ifdef TRINITY_HAS_OPENCV
        const std::string visionImg =
            (std::filesystem::temp_directory_path() / "trinity_selftest.png").string();
        const cv::Mat probe(8, 8, CV_8UC3, cv::Scalar(10, 20, 30));
        cv::imwrite(visionImg, probe);
        visionReq.parameters = trinity::core::Json{{"path", visionImg}};
        const auto visionResult = context.engines().execute(visionReq);
        visionOk = visionResult.success &&
                   visionResult.result["input"]["metadata"]["width"] == 8;
        std::remove(visionImg.c_str());
#else
        visionReq.parameters = trinity::core::Json{{"path", "missing.png"}};
        const auto visionResult = context.engines().execute(visionReq);
        visionOk = !visionResult.success && !visionResult.errors.empty();
#endif
    } catch (...) {
        visionOk = false;
    }
    std::cout << (visionOk ? "[PASS] " : "[FAIL] ") << "vision_image_ops\n";
    // Simulation must run a deterministic closed-form linear motion.
    bool simOk = false;
    try {
        trinity::engines::EngineRequest simReq;
        simReq.engine = "simulation";
        simReq.operation = "simulate_linear_motion";
        simReq.parameters = trinity::core::Json{
            {"duration_s", 1.0},
            {"initial_velocity_m_s", 2.0},
            {"acceleration_m_s2", 1.0},
            {"write_artifacts", false}};
        const auto simResult = context.engines().execute(simReq);
        simOk = simResult.success &&
                simResult.result["result"].value("step_count", 0LL) > 0;
    } catch (...) {
        simOk = false;
    }
    std::cout << (simOk ? "[PASS] " : "[FAIL] ") << "simulation_runs_linear_motion\n";
    // Research must index a document and return it from a deterministic search.
    bool researchOk = false;
    try {
        trinity::engines::EngineRequest indexReq;
        indexReq.engine = "research";
        indexReq.operation = "index_document";
        indexReq.parameters = trinity::core::Json{
            {"doc_id", "selftest-note"},
            {"title", "Selftest note"},
            {"text", "Trinity research engine indexes documents deterministically."}};
        const auto indexed = context.engines().execute(indexReq);
        trinity::engines::EngineRequest searchReq;
        searchReq.engine = "research";
        searchReq.operation = "search";
        searchReq.parameters = trinity::core::Json{{"query", "indexes documents"}};
        const auto found = context.engines().execute(searchReq);
        researchOk = indexed.success && found.success &&
                     found.result.value("hit_count", 0) == 1 &&
                     found.validation.has_value() && found.validation->passed();
    } catch (...) {
        researchOk = false;
    }
    std::cout << (researchOk ? "[PASS] " : "[FAIL] ") << "research_indexes_and_searches\n";
    // Robotics must solve deterministic DH forward kinematics.
    bool roboticsOk = false;
    try {
        trinity::engines::EngineRequest fkReq;
        fkReq.engine = "robotics";
        fkReq.operation = "forward_kinematics";
        fkReq.parameters =
            trinity::core::Json{{"joint_angles", trinity::core::Json::array({0.0, 0.0})}};
        const auto fkResult = context.engines().execute(fkReq);
        const auto& pos = fkResult.result["end_effector"]["position"];
        roboticsOk = fkResult.success &&
                     std::fabs(pos.value("x", 0.0) - 2.0) < 1e-6 &&
                     std::fabs(pos.value("y", 0.0)) < 1e-6 &&
                     std::fabs(pos.value("z", 0.0)) < 1e-6 &&
                     fkResult.validation.has_value() && fkResult.validation->passed();
    } catch (...) {
        roboticsOk = false;
    }
    std::cout << (roboticsOk ? "[PASS] " : "[FAIL] ") << "robotics_forward_kinematics\n";
    // Model seam must refuse truthfully without an LLM.
    trinity::intelligence::ModelRequest request;
    request.prompt = "selftest";
    const auto response = context.model().generate(request);
    const bool modelOk = !response.success && !response.error.is_null();
    std::cout << (modelOk ? "[PASS] " : "[FAIL] ") << "model_refuses_without_llm\n";
    // Planner with no model must execute nothing (default Null path).
    bool plannerOk = false;
    try {
        trinity::intelligence::Planner planner(context.model(), context.jobs(),
                                               context.engines());
        const auto plan = planner.planAndExecute(request);
        plannerOk = !plan.success && plan.steps.empty() && !plan.error.is_null();
    } catch (...) {
        plannerOk = false;
    }
    std::cout << (plannerOk ? "[PASS] " : "[FAIL] ") << "planner_runs_nothing_without_llm\n";
    // Provider selection honors env with safe fallback to null.
    const bool providerOk =
        !context.config().model.providerId.empty() &&
        trinity::intelligence::ModelProviderFactory::instance().has(
            context.model().info().providerId);
    std::cout << (providerOk ? "[PASS] " : "[FAIL] ") << "provider_selection_valid\n";
    allOk = allOk && missOk && mathOk && refuseOk && cadOk && pcbOk && fwOk &&
            visionOk && simOk && researchOk && roboticsOk && modelOk && plannerOk &&
            providerOk;
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
        out.capabilities = entry.capabilities;
        out.lastResult = entry.lastResult;
        out.implemented = entry.implemented;
        uiSummary.engines.push_back(std::move(out));
    }
    uiSummary.modelProvider = summary.modelProvider;
    uiSummary.modelAvailable = context.model().info().available;
    uiSummary.coreOk = status.isOk();

    trinity::ui::MainWindow window(uiSummary, &context.engines(), &context.jobs(),
                                     &context.executor(), &context.pipeline());
    window.setViewerServices(&context.artifactRepository(), &context.artifacts());
    window.setWorkerService(&context.worker());
    window.show();
    const int code = app.exec();

    context.shutdown();
    return code;
#else
    std::cerr << "Trinity built without Qt; run with --selftest.\n";
    return runSelftest() == 0 ? 0 : 1;
#endif
}
