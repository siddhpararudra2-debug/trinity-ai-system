#include <doctest.h>

#include <chrono>
#include <filesystem>
#include <thread>
#include <utility>

#include "trinity/artifacts/Artifact.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/PcbEngine.hpp"
#include "trinity/intelligence/RequestPipeline.hpp"
#include "trinity/jobs/Job.hpp"
#include "trinity/jobs/JobWorker.hpp"
#include "trinity/pcb/KiCadExport.hpp"
#include "trinity/pcb/Validators.hpp"
#include "trinity/storage/Database.hpp"
#include "trinity/storage/Repositories.hpp"
#include "trinity/workflows/Executor.hpp"
#include "trinity/workflows/Workflow.hpp"

namespace {

trinity::engines::EngineRequest makeReq(const std::string& op,
                                        const trinity::core::Json& params) {
    trinity::engines::EngineRequest req;
    req.engine = "pcb";
    req.operation = op;
    req.parameters = params;
    return req;
}

trinity::core::Json makeBoard(trinity::engines::PcbEngine& engine) {
    const auto out =
        engine.execute(makeReq("create_board", {{"width_mm", 50.0},
                                                {"height_mm", 40.0},
                                                {"thickness_mm", 1.6},
                                                {"components",
                                                 {{{"part", "esp32"}},
                                                  {{"part", "regulator"}}}}}));
    REQUIRE(out.success);
    return out.result["design"];
}

struct Fixture {
    std::string dir;
    std::shared_ptr<trinity::storage::Database> db;
    std::shared_ptr<trinity::engines::EngineRegistry> registry;
    std::shared_ptr<trinity::artifacts::ArtifactManager> artifacts;
    std::shared_ptr<trinity::jobs::JobManager> jobs;
    std::shared_ptr<trinity::storage::WorkflowRepository> workflows;
    std::shared_ptr<trinity::workflows::WorkflowExecutor> executor;
    std::shared_ptr<trinity::intelligence::RequestPipeline> pipeline;

    Fixture() {
        dir = (std::filesystem::temp_directory_path() / "trinity-test-pcb").string();
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir + "/artifacts");
        db = std::make_shared<trinity::storage::Database>(dir + "/trinity.db");
        db->init();
        registry = std::make_shared<trinity::engines::EngineRegistry>();
        registry->registerEngine(std::make_shared<trinity::engines::PcbEngine>());
        artifacts = std::make_shared<trinity::artifacts::ArtifactManager>(db, dir + "/artifacts");
        jobs = std::make_shared<trinity::jobs::JobManager>(db, registry, artifacts);
        workflows = std::make_shared<trinity::storage::WorkflowRepository>(db);
        executor = std::make_shared<trinity::workflows::WorkflowExecutor>(jobs, registry,
                                                                          workflows);
        pipeline = std::make_shared<trinity::intelligence::RequestPipeline>(jobs, registry);
    }
    ~Fixture() {
        pipeline.reset();
        executor.reset();
        workflows.reset();
        jobs.reset();
        artifacts.reset();
        registry.reset();
        db.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

}  // namespace

TEST_CASE("pcb IR serializes and round-trips") {
    trinity::pcb::PcbDesign design;
    design.board.widthMm = 50.0;
    design.board.heightMm = 40.0;
    trinity::pcb::Component comp;
    comp.ref = "U1";
    comp.value = "ESP32";
    comp.footprint = "ESP32-WROOM-32";
    comp.type = trinity::pcb::ComponentType::Mcu;
    trinity::pcb::Pin pin;
    pin.number = "1";
    pin.name = "3V3";
    pin.electrical = trinity::pcb::PinElectricalType::PowerIn;
    comp.pins.push_back(pin);
    design.components.push_back(comp);
    const auto back = trinity::pcb::PcbDesign::fromJson(design.toJson());
    CHECK(back.board.widthMm == doctest::Approx(50.0));
    REQUIRE(back.components.size() == 1);
    CHECK(back.components[0].ref == "U1");
    CHECK(trinity::pcb::toString(back.components[0].type) == "mcu");
    CHECK(trinity::pcb::toString(back.components[0].pins[0].electrical) == "power_in");
    CHECK(trinity::pcb::toString(trinity::pcb::ComponentType::Imu) == "imu");
    CHECK(trinity::pcb::toString(trinity::pcb::componentTypeFromString("regulator")) ==
          "regulator");
    std::string ref, num;
    CHECK(trinity::pcb::splitPinRef("U1.19", ref, num));
    CHECK(ref == "U1");
    CHECK(num == "19");
    CHECK_FALSE(trinity::pcb::splitPinRef("nope", ref, num));
}

TEST_CASE("pcb create_board accepts valid and rejects invalid dimensions") {
    trinity::engines::PcbEngine engine;
    CHECK(engine.name() == "pcb");
    const auto out =
        engine.execute(makeReq("create_board", {{"width_mm", 50.0},
                                                {"height_mm", 40.0}}));
    CHECK(out.success);
    CHECK(out.result["board"]["width_mm"] == doctest::Approx(50.0));
    REQUIRE(out.validation.has_value());
    CHECK(out.validation->passed());

    CHECK_FALSE(engine.execute(makeReq("create_board", {{"width_mm", -5.0},
                                                        {"height_mm", 40.0}}))
                    .success);
    CHECK_FALSE(engine.execute(makeReq("create_board", {{"width_mm", 50.0},
                                                        {"height_mm", 0.0}}))
                    .success);
    CHECK_FALSE(engine.execute(makeReq("create_board", {{"width_mm", 50.0},
                                                        {"height_mm", 40.0},
                                                        {"thickness_mm", 99.0}}))
                    .success);
    CHECK_FALSE(engine.execute(makeReq("create_board", {{"width_mm", 50.0}})).success);
}

TEST_CASE("pcb add_component rejects duplicates and unknown footprints") {
    trinity::engines::PcbEngine engine;
    const auto design = makeBoard(engine);
    const auto ok = engine.execute(
        makeReq("add_component", {{"design", design},
                                  {"ref", "R1"},
                                  {"value", "10k"},
                                  {"footprint", "0603"}}));
    CHECK(ok.success);
    CHECK(ok.result["design"]["components"].size() == 3);

    const auto dup = engine.execute(
        makeReq("add_component", {{"design", design},
                                  {"ref", "U1"},
                                  {"value", "x"},
                                  {"footprint", "0603"}}));
    CHECK_FALSE(dup.success);

    const auto unknown = engine.execute(
        makeReq("add_component", {{"design", design},
                                  {"ref", "X9"},
                                  {"value", "x"},
                                  {"footprint", "BGA-9999"}}));
    CHECK_FALSE(unknown.success);
}

TEST_CASE("pcb nets validate pins and power connections") {
    trinity::engines::PcbEngine engine;
    auto design = makeBoard(engine);
    // U1 = ESP32 (pins 1..19, 20..38), U2 = SOT-223 regulator (pins 1..4).
    const auto gnd = engine.execute(makeReq("add_net", {{"design", design},
                                                        {"name", "GND"},
                                                        {"pins", {"U1.19", "U2.2"}}}));
    CHECK(gnd.success);
    design = gnd.result["design"];

    // Unknown pin reference.
    CHECK_FALSE(engine.execute(makeReq("add_net", {{"design", design},
                                                   {"name", "BAD"},
                                                   {"pins", {"U1.999"}}}))
                    .success);
    // Unknown component.
    CHECK_FALSE(engine.execute(makeReq("add_net", {{"design", design},
                                                   {"name", "BAD"},
                                                   {"pins", {"U9.1"}}}))
                    .success);
    // Duplicate pins within one net.
    CHECK_FALSE(engine.execute(makeReq("add_net", {{"design", design},
                                                   {"name", "DUP"},
                                                   {"pins", {"U1.1", "U1.1"}}}))
                    .success);

    // Power pins left floating: validation must fail.
    const auto invalid = engine.execute(makeReq("validate_design", {{"design", design}}));
    CHECK_FALSE(invalid.success);

    // Net the remaining power pins: 3V3 (U1.1), second GND (U1.20),
    // regulator VIN/VOUT/GND-tab.
    auto withPwr = engine.execute(makeReq("add_net", {{"design", design},
                                                      {"name", "PWR"},
                                                      {"pins", {"U1.1", "U2.1"}}}));
    CHECK(withPwr.success);
    design = withPwr.result["design"];
    auto withRest = engine.execute(makeReq("add_net", {{"design", design},
                                                       {"name", "GND2"},
                                                       {"pins", {"U1.20", "U1.38",
                                                                 "U2.4", "U2.3"}}}));
    CHECK(withRest.success);
    design = withRest.result["design"];
    const auto valid = engine.execute(makeReq("validate_design", {{"design", design}}));
    CHECK(valid.success);
    CHECK(valid.result.value("passed", false));
}

TEST_CASE("pcb placement enforces bounds and rotation") {
    trinity::engines::PcbEngine engine;
    const auto design = makeBoard(engine);
    // ESP32 body is 18 x 25.5: center (25, 20) fits a 50x40 board.
    const auto ok = engine.execute(makeReq("place_component", {{"design", design},
                                                               {"ref", "U1"},
                                                               {"x_mm", 25.0},
                                                               {"y_mm", 20.0},
                                                               {"rotation_deg", 0.0}}));
    CHECK(ok.success);

    // Out of bounds.
    CHECK_FALSE(engine.execute(makeReq("place_component", {{"design", design},
                                                           {"ref", "U1"},
                                                           {"x_mm", 500.0},
                                                           {"y_mm", 20.0}}))
                    .success);
    // Bad rotation.
    CHECK_FALSE(engine.execute(makeReq("place_component", {{"design", design},
                                                           {"ref", "U1"},
                                                           {"x_mm", 25.0},
                                                           {"y_mm", 20.0},
                                                           {"rotation_deg", 45.0}}))
                    .success);
    // Unknown ref.
    CHECK_FALSE(engine.execute(makeReq("place_component", {{"design", design},
                                                           {"ref", "U9"},
                                                           {"x_mm", 25.0},
                                                           {"y_mm", 20.0}}))
                    .success);
}

TEST_CASE("pcb overlap and clearance are detected") {
    trinity::engines::PcbEngine engine;
    auto design = makeBoard(engine);
    // Fully net power pins first so only geometry rules fire.
    for (const auto& [name, pins] :
         {std::pair<const char*, std::vector<std::string>>{"GND", {"U1.19", "U2.2"}},
          {"PWR", {"U1.1", "U2.1"}},
          {"GND2", {"U1.20", "U1.38", "U2.4", "U2.3"}}}) {
        trinity::core::Json pinJson = trinity::core::Json::array();
        for (const auto& pin : pins) {
            pinJson.push_back(pin);
        }
        const auto net =
            engine.execute(makeReq("add_net", {{"design", design},
                                               {"name", name},
                                               {"pins", pinJson}}));
        REQUIRE(net.success);
        design = net.result["design"];
    }
    // Stack both bodies at the same spot: overlap error.
    REQUIRE(engine
                .execute(makeReq("place_component", {{"design", design},
                                                     {"ref", "U1"},
                                                     {"x_mm", 25.0},
                                                     {"y_mm", 20.0}}))
                .success);
    auto placed = engine.execute(makeReq("place_component", {{"design", design},
                                                             {"ref", "U1"},
                                                             {"x_mm", 25.0},
                                                             {"y_mm", 20.0}}));
    design = placed.result["design"];
    REQUIRE(engine
                .execute(makeReq("place_component", {{"design", design},
                                                     {"ref", "U2"},
                                                     {"x_mm", 25.0},
                                                     {"y_mm", 20.0}}))
                .success);
    placed = engine.execute(makeReq("place_component", {{"design", design},
                                                        {"ref", "U2"},
                                                        {"x_mm", 25.0},
                                                        {"y_mm", 20.0}}));
    const auto invalid =
        engine.execute(makeReq("validate_design", {{"design", placed.result["design"]}}));
    CHECK_FALSE(invalid.success);

    // Separated placements validate (U2 SOT-223 at corner, clear of U1).
    auto apart = engine.execute(makeReq("place_component", {{"design", design},
                                                            {"ref", "U2"},
                                                            {"x_mm", 5.0},
                                                            {"y_mm", 5.0}}));
    REQUIRE(apart.success);
    const auto valid =
        engine.execute(makeReq("validate_design", {{"design", apart.result["design"]}}));
    CHECK(valid.success);
}

TEST_CASE("pcb KiCad export is structurally valid") {
    trinity::engines::PcbEngine engine;
    auto design = makeBoard(engine);
    for (const auto& [name, pins] :
         {std::pair<const char*, std::vector<std::string>>{"GND", {"U1.19", "U2.2"}},
          {"PWR", {"U1.1", "U2.1"}},
          {"GND2", {"U1.20", "U1.38", "U2.4", "U2.3"}}}) {
        trinity::core::Json pinJson = trinity::core::Json::array();
        for (const auto& pin : pins) {
            pinJson.push_back(pin);
        }
        const auto net =
            engine.execute(makeReq("add_net", {{"design", design},
                                               {"name", name},
                                               {"pins", pinJson}}));
        REQUIRE(net.success);
        design = net.result["design"];
    }
    REQUIRE(engine
                .execute(makeReq("place_component", {{"design", design},
                                                     {"ref", "U1"},
                                                     {"x_mm", 25.0},
                                                     {"y_mm", 20.0}}))
                .success);
    auto placed = engine.execute(makeReq("place_component", {{"design", design},
                                                             {"ref", "U1"},
                                                             {"x_mm", 25.0},
                                                             {"y_mm", 20.0}}));
    design = placed.result["design"];
    REQUIRE(engine
                .execute(makeReq("place_component", {{"design", design},
                                                     {"ref", "U2"},
                                                     {"x_mm", 5.0},
                                                     {"y_mm", 5.0}}))
                .success);
    placed = engine.execute(makeReq("place_component", {{"design", design},
                                                        {"ref", "U2"},
                                                        {"x_mm", 5.0},
                                                        {"y_mm", 5.0}}));
    const auto exported = trinity::pcb::exportKicadPcb(
        trinity::pcb::PcbDesign::fromJson(placed.result["design"]), "demo");
    CHECK(exported.ok);
    CHECK(exported.error.empty());
    CHECK(exported.content.find("(kicad_pcb") != std::string::npos);
    CHECK(exported.content.find("Edge.Cuts") != std::string::npos);
    CHECK(exported.content.find("(footprint \"ESP32-WROOM-32\"") != std::string::npos);
    std::string problem;
    CHECK(trinity::pcb::verifyKicadPcb(exported.content, problem));
    CHECK_FALSE(trinity::pcb::verifyKicadPcb("(kicad_pcb (unbalanced", problem));
}

TEST_CASE("pcb export refuses invalid designs without files") {
    trinity::engines::PcbEngine engine;
    const auto design = makeBoard(engine);  // power pins floating
    const auto out = engine.execute(
        makeReq("export", {{"design", design}, {"project", "bad"}}));
    CHECK_FALSE(out.success);
    CHECK(out.pendingArtifacts.empty());
}

TEST_CASE("pcb unsupported operations refuse truthfully") {
    trinity::engines::PcbEngine engine;
    const auto out =
        engine.execute(makeReq("route_traces", {{"design", trinity::core::Json::object()}}));
    CHECK_FALSE(out.success);
    REQUIRE_FALSE(out.errors.empty());
    CHECK(out.errors.front().value("code", "") == "capability_unavailable");
}

TEST_CASE("pcb artifacts register with SHA-256 through jobs") {
    Fixture fx;
    trinity::engines::PcbEngine engine;
    auto design = makeBoard(engine);
    // Persist through the real JobManager: export promotes scratch files
    // into managed storage with checksums.
    auto placed = fx.jobs->runSync("pcb", "place_component",
                                   {{"design", design},
                                    {"ref", "U1"},
                                    {"x_mm", 25.0},
                                    {"y_mm", 20.0}});
    CHECK(placed["success"] == true);
    design = placed["result"]["design"];
    auto placed2 = fx.jobs->runSync("pcb", "place_component",
                                    {{"design", design},
                                     {"ref", "U2"},
                                     {"x_mm", 5.0},
                                     {"y_mm", 5.0}});
    CHECK(placed2["success"] == true);
    design = placed2["result"]["design"];
    for (const auto& [name, pins] :
         {std::pair<const char*, std::vector<std::string>>{"GND", {"U1.19", "U2.2"}},
          {"PWR", {"U1.1", "U2.1"}},
          {"GND2", {"U1.20", "U1.38", "U2.4", "U2.3"}}}) {
        trinity::core::Json pinJson = trinity::core::Json::array();
        for (const auto& pin : pins) {
            pinJson.push_back(pin);
        }
        auto net = fx.jobs->runSync("pcb", "add_net",
                                    {{"design", design},
                                     {"name", name},
                                     {"pins", pinJson}});
        CHECK(net["success"] == true);
        design = net["result"]["design"];
    }
    const auto exported = fx.jobs->runSync(
        "pcb", "export", {{"design", design}, {"project", "job_board"}});
    CHECK(exported["success"] == true);
    REQUIRE(exported["artifacts"].size() == 2);
    CHECK(exported["artifacts"][0]["checksum"].get<std::string>().size() == 64);
    CHECK(exported["artifacts"][0]["size_bytes"].get<long long>() > 100);
    // SQLite holds the artifact record.
    const std::string artifactId = exported["artifacts"][0]["artifact_id"];
    const auto stored = fx.artifacts->get(artifactId);
    CHECK(stored.checksum.size() == 64);
    CHECK(stored.jobId == exported["job_id"].get<std::string>());
    CHECK(std::filesystem::is_regular_file(stored.path));
    CHECK(std::filesystem::file_size(stored.path) > 100);
}

TEST_CASE("pcb executes on the worker thread with persistence") {
    Fixture fx;
    auto worker = std::make_shared<trinity::jobs::JobWorker>(fx.jobs, 1);
    worker->start();
    const std::string jobId = worker->submit("pcb", "create_board",
                                             {{"width_mm", 50.0}, {"height_mm", 40.0}});
    trinity::jobs::Job job;
    for (int i = 0; i < 200; ++i) {
        job = fx.jobs->get(jobId);
        if (trinity::jobs::isTerminal(job.status)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    CHECK(trinity::jobs::isTerminal(job.status));
    CHECK(job.succeeded());
    CHECK_FALSE(job.startedAt.empty());
    CHECK_FALSE(job.completedAt.empty());
    worker->stop();
}

TEST_CASE("pcb workflow chains create-add-place-validate") {
    Fixture fx;
    trinity::workflows::Workflow wf;
    wf.workflowId = "wf-pcb";
    wf.name = "pcb-chain";
    wf.status = trinity::workflows::WorkflowStatus::Queued;
    wf.createdAt = "2026-01-01T00:00:00+00:00";
    wf.updatedAt = wf.createdAt;

    trinity::workflows::WorkflowNode a;
    a.id = "A";
    a.nodeId = "A";
    a.engine = "pcb";
    a.operation = "create_board";
    a.parameters = {{"width_mm", 50.0},
                    {"height_mm", 40.0},
                    {"components", {{{"part", "esp32"}}, {{"part", "regulator"}}}}};
    a.input = a.parameters;

    trinity::workflows::WorkflowNode b;
    b.id = "B";
    b.nodeId = "B";
    b.engine = "pcb";
    b.operation = "add_net";
    b.parameters = {{"name", "GND"}, {"pins", {"U1.19", "U2.2"}}};
    b.input = b.parameters;
    b.inputFrom = {{"design", "{{A.design}}"}};

    trinity::workflows::WorkflowNode c;
    c.id = "C";
    c.nodeId = "C";
    c.engine = "pcb";
    c.operation = "place_component";
    c.parameters = {{"ref", "U1"}, {"x_mm", 25.0}, {"y_mm", 20.0}};
    c.input = c.parameters;
    c.inputFrom = {{"design", "{{B.design}}"}};

    trinity::workflows::WorkflowNode d;
    d.id = "D";
    d.nodeId = "D";
    d.engine = "pcb";
    d.operation = "export";
    d.parameters = {{"project", "wf_board"}};
    d.input = d.parameters;
    d.inputFrom = {{"design", "{{C.design}}"}};

    wf.nodes = {a, b, c, d};
    // Linear edges A->B->C->D.
    int edge = 0;
    for (const auto& [from, to] :
         {std::pair<const char*, const char*>{"A", "B"},
          {"B", "C"},
          {"C", "D"}}) {
        trinity::workflows::WorkflowEdge e;
        e.edgeId = "e" + std::to_string(++edge);
        e.fromNode = from;
        e.toNode = to;
        wf.edges.push_back(e);
    }
    const auto result = fx.executor->runInline(wf);
    // Export is the terminal node: power pins are still floating, so the
    // chain honestly ends at a refused export rather than a fake file.
    CHECK(result.nodeResults.count("A") == 1);
    CHECK(result.nodeResults.at("A")["design"]["components"].size() == 2);
    CHECK(result.nodeResults.count("B") == 1);
    CHECK(result.nodeResults.count("C") == 1);
}

TEST_CASE("pcb full request pipeline creates boards") {
    Fixture fx;
    auto out = fx.pipeline->executeSync("Create a 50 mm x 40 mm PCB with an ESP32");
    CHECK(out.success);
    CHECK(out.routing.engine == "pcb");
    CHECK(out.routing.operation == "create_board");
    CHECK(out.engineEnvelope["result"]["board"]["width_mm"] == doctest::Approx(50.0));
    CHECK(out.engineEnvelope["result"]["board"]["height_mm"] == doctest::Approx(40.0));
    REQUIRE_FALSE(out.jobId.empty());
    const auto job = fx.jobs->get(out.jobId);
    CHECK(trinity::jobs::toString(job.status) == "completed");

    // Incomplete placement request: no job, no invented board.
    auto missing = fx.pipeline->executeSync("Place U1 at the center of the board");
    CHECK_FALSE(missing.success);
    CHECK(missing.jobId.empty());
}
