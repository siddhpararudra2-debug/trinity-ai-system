// Trinity — math engine + job system + validation lifecycle tests.
#include <filesystem>

#include <doctest/doctest.h>

#include "../artifacts/Artifact.hpp"
#include "../db/Schema.hpp"
#include "../engines/math/MathEngine.hpp"
#include "../jobs/JobSystem.hpp"
#include "../validation/ValidationEngine.hpp"

using namespace trinity;

TEST_CASE("expression evaluator handles arithmetic and functions") {
    using trinity::engines::math::evaluate_expression;
    CHECK(evaluate_expression("1 + 2 * 3", {}) == doctest::Approx(7.0));
    CHECK(evaluate_expression("(1 + 2) * 3", {}) == doctest::Approx(9.0));
    CHECK(evaluate_expression("2 ^ 3 ^ 2", {}) == doctest::Approx(512.0));  // right assoc
    CHECK(evaluate_expression("-4 + 10", {}) == doctest::Approx(6.0));
    CHECK(evaluate_expression("sqrt(16)", {}) == doctest::Approx(4.0));
    CHECK(evaluate_expression("abs(3 - 8)", {}) == doctest::Approx(5.0));
    CHECK(evaluate_expression("20 % 3", {}) == doctest::Approx(2.0));
    std::map<std::string, double> vars{{"r", 2.5}};
    CHECK(evaluate_expression("pi * r^2", vars) == doctest::Approx(19.6349540849));
}

TEST_CASE("evaluator rejects malformed and dangerous input") {
    using trinity::engines::math::evaluate_expression;
    CHECK_THROWS(evaluate_expression("1 +", {}));
    CHECK_THROWS(evaluate_expression("unknown_var + 1", {}));
    CHECK_THROWS(evaluate_expression("1/0", {}));
    CHECK_THROWS(evaluate_expression("sqrt(-1)", {}));
}

TEST_CASE("linear and quadratic solves verify roots by re-substitution") {
    using namespace trinity::engines::math;
    MathEngine engine;
    core::Json request = core::Json::parse(R"({"expression": "2*x + 4 = 0"})");
    auto output = engine.execute("solve", request);
    CHECK(output.success);
    CHECK(output.validation_status == "VALIDATED");
    CHECK(output.result.find("result")->as_array().size() == 1);
    CHECK(output.result.find("result")->as_array().at(0).as_double() == doctest::Approx(-2.0));

    core::Json quadratic = core::Json::parse(R"({"expression": "x^2 - 4 = 0"})");
    auto output2 = engine.execute("solve", quadratic);
    CHECK(output2.success);
    const auto roots = output2.result.find("result")->as_array();
    REQUIRE(roots.size() == 2);
    std::vector<double> values{roots[0].as_double(), roots[1].as_double()};
    std::sort(values.begin(), values.end());
    CHECK(values[0] == doctest::Approx(-2.0));
    CHECK(values[1] == doctest::Approx(2.0));
}

TEST_CASE("identity checks verify without unknowns") {
    using namespace trinity::engines::math;
    MathEngine engine;
    auto output = engine.execute("solve", core::Json::parse(R"({"expression": "1 + 1 = 2"})"));
    CHECK(output.success);
    CHECK(output.validation_status == "VERIFIED");
    CHECK(output.result.find("identity_holds")->as_bool());
}

TEST_CASE("job system runs jobs async and reports state") {
    const std::string dir =
        std::filesystem::temp_directory_path().string() + "/trinity_test_jobs_" +
        trinity::core::new_uuid().substr(0, 8);
    auto opened = db::open_and_migrate(dir + "/test.db");
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());
    artifacts::ArtifactStore store(db, dir + "/artifacts");
    jobs::JobSystem jobs(db, store, 2);

    std::atomic<int> executed{0};
    auto submitted = jobs.submit(
        "test", "echo", core::Json::parse(R"({"n": 1})"),
        [&](jobs::JobContext& context, const core::Json& request) {
            context.log("working");
            context.report_progress(0.5);
            ++executed;
            core::Json out = core::Json::object();
            out["echo"] = request.find("n")->as_int();
            return out;
        });
    REQUIRE(submitted.is_ok());
    jobs.wait_for_idle();

    auto record = jobs.get(submitted.value());
    REQUIRE(record.is_ok());
    CHECK(record.value().status == jobs::JobState::Completed);
    CHECK(executed.load() == 1);
    CHECK(record.value().duration_ms >= 0);
    CHECK(record.value().output.find("echo")->as_int() == 1);

    std::error_code ec;
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("job cancellation moves job to CANCELLED") {
    const std::string dir =
        std::filesystem::temp_directory_path().string() + "/trinity_test_cancel_" +
        trinity::core::new_uuid().substr(0, 8);
    auto opened = db::open_and_migrate(dir + "/test.db");
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());
    artifacts::ArtifactStore store(db, dir + "/artifacts");
    jobs::JobSystem jobs(db, store, 1);

    auto slow = jobs.submit("test", "slow", core::Json::object(),
                            [&](jobs::JobContext& context, const core::Json&) {
                                for (int i = 0; i < 100 && context.should_run(); ++i) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                }
                                return core::Json::object();
                            });
    REQUIRE(slow.is_ok());
    // Give the worker a moment to pick it up, then cancel.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(jobs.cancel(slow.value()).is_ok());
    jobs.wait_for_idle();
    auto record = jobs.get(slow.value());
    REQUIRE(record.is_ok());
    CHECK(record.value().status == jobs::JobState::Cancelled);
    std::error_code ec;
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("artifact store hashes and round trips metadata") {
    const std::string dir =
        std::filesystem::temp_directory_path().string() + "/trinity_test_art_" +
        trinity::core::new_uuid().substr(0, 8);
    auto opened = db::open_and_migrate(dir + "/test.db");
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());
    artifacts::ArtifactStore store(db, dir + "/artifacts");

    const std::string source = dir + "/payload.txt";
    std::error_code write_ec;
    core::FileSystem::write_file_atomic(source, "trinity artifact payload", write_ec);
    auto stored = store.store_file(source, "txt", "proj1", "job1", "cad", "1.0");
    REQUIRE(stored.is_ok());
    CHECK(stored.value().hash_sha256 ==
          trinity::core::sha256_hex("trinity artifact payload"));
    CHECK(stored.value().validation_state == artifacts::ValidationState::Generated);

    auto fetched = store.get(stored.value().artifact_id);
    REQUIRE(fetched.is_ok());
    CHECK(fetched.value().size_bytes == stored.value().size_bytes);

    auto integrity = store.verify_integrity(stored.value().artifact_id);
    REQUIRE(integrity.is_ok());
    CHECK(integrity.value());

    SUBCASE("state machine guards illegal jumps") {
        validation::ValidationEngine validation(db, store);
        auto jump = validation.apply_checks(stored.value().artifact_id, "cad",
                                            core::Json::object(), true, true);
        CHECK(jump.is_error());  // GENERATED -> VERIFIED is illegal

        auto validated = validation.apply_checks(stored.value().artifact_id, "cad",
                                                 core::Json::object(), true, false);
        CHECK(validated.is_ok());
        CHECK(validated.value() == artifacts::ValidationState::Validated);

        auto verified = validation.apply_checks(stored.value().artifact_id, "cad",
                                                core::Json::object(), true, true);
        CHECK(verified.is_ok());
        CHECK(verified.value() == artifacts::ValidationState::Verified);
    }

    SUBCASE("unknown artifact types are rejected") {
        auto bad = store.store_file(source, "definitely_not_a_type", "", "", "", "");
        CHECK(bad.is_error());
    }

    std::error_code ec;
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("workflow dag rejects cycles and duplicates") {
    std::vector<workflows::WorkflowNode> nodes{
        {"build", "cad", "generate", core::Json::object(), {"validate"}},
        {"validate", "cad", "validate", core::Json::object(), {}}};
    auto ordered = workflows::topological_order(nodes);
    CHECK(ordered.is_ok());
    CHECK(ordered.value().at(0).id == "validate");

    std::vector<workflows::WorkflowNode> cyclic{
        {"a", "cad", "generate", core::Json::object(), {"b"}},
        {"b", "cad", "validate", core::Json::object(), {"a"}}};
    CHECK(workflows::topological_order(cyclic).is_error());

    std::vector<workflows::WorkflowNode> duplicates{
        {"x", "cad", "generate", core::Json::object(), {}},
        {"x", "cad", "validate", core::Json::object(), {}}};
    CHECK(workflows::topological_order(duplicates).is_error());
}

TEST_CASE("command parser handles the flagship command deterministically") {
    using namespace trinity::commands;
    Command command = parse_command("Create a 50 mm quadcopter frame");
    CHECK(command.matched);
    CHECK(command.verb == CommandVerb::Create);
    CHECK(command.object == "quadcopter_frame");
    CHECK(command.parameters.find("overall_size")->as_double() == doctest::Approx(50.0));

    Command calculate = parse_command("calculate 2*pi*5");
    CHECK(calculate.matched);
    CHECK(calculate.verb == CommandVerb::Calculate);

    Command unknown = parse_command("make me a sandwich please");
    CHECK_FALSE(unknown.matched);
}

TEST_CASE("tool executor refuses scaffolded engines") {
    const std::string dir =
        std::filesystem::temp_directory_path().string() + "/trinity_test_exec_" +
        trinity::core::new_uuid().substr(0, 8);
    auto opened = db::open_and_migrate(dir + "/test.db");
    REQUIRE(opened.is_ok());
    db::Database db = std::move(opened.value());
    artifacts::ArtifactStore store(db, dir + "/artifacts");
    jobs::JobSystem jobs(db, store, 1);
    engines::bootstrap_builtin_engines();

    commands::ToolExecutor executor(jobs);
    auto refused = executor.run_text("generate a pcb layout", "");
    CHECK(refused.is_error());
    CHECK(refused.error().code() == core::ErrorCode::CapabilityUnavailableError);
    std::error_code ec;
    core::FileSystem::remove_all(dir, ec);
}

TEST_CASE("ipc frames round trip and classify errors") {
    ipc::IpcServer server;
    server.register_method("add", [](const core::Json& payload) {
        core::Json out = core::Json::object();
        out["sum"] = payload.find("a")->as_double() + payload.find("b")->as_double();
        return out;
    });
    server.register_method("boom", [](const core::Json&) -> core::Json {
        throw core::TrinityException(
            core::Error(core::ErrorCode::EngineExecutionError, "deterministic failure"));
    });

    auto ok = server.call("add", core::Json::parse(R"({"a": 2, "b": 3})"));
    REQUIRE(ok.is_ok());
    CHECK(ok.value().find("sum")->as_double() == doctest::Approx(5.0));

    auto failed = server.call("boom", core::Json::object());
    REQUIRE(failed.is_error());
    CHECK(failed.error().code() == core::ErrorCode::EngineExecutionError);

    auto missing = server.call("nope", core::Json::object());
    REQUIRE(missing.is_error());
    CHECK(missing.error().code() == core::ErrorCode::RequestValidationError);

    auto bad_frame = ipc::Frame::decode("this is not json");
    CHECK(bad_frame.is_error());
    CHECK(bad_frame.error().code() == core::ErrorCode::IpcProtocolError);
}

TEST_CASE("model layer ships null by default and refuses honestly") {
    auto& hub = model::ModelHub::instance();
    CHECK_FALSE(hub.provider().info().available);
    CHECK_FALSE(hub.cad_generator().available());
    CHECK_FALSE(hub.pcb_generator().available());
    CHECK_FALSE(hub.reasoner().available());

    auto refused = hub.cad_generator().generate_ir({"quadcopter_frame", {}, ""});
    CHECK(refused.is_error());
    CHECK(refused.error().code() == core::ErrorCode::CapabilityUnavailableError);
}
