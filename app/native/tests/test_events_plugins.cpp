// Trinity — event bus, plugin manager and command registry tests.
#include <doctest/doctest.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../commands/CommandRegistry.hpp"
#include "../core/EventBus.hpp"
#include "../core/FileSystem.hpp"
#include "../core/Uuid.hpp"
#include "../plugins/PluginManager.hpp"

using namespace trinity;

namespace {

std::string fresh_temp_dir(const std::string& label) {
    const std::filesystem::path base =
        std::filesystem::temp_directory_path() /
        ("trinity_" + label + "_" + core::new_uuid().substr(0, 8));
    std::error_code ec;
    core::FileSystem::ensure_directory(base.string(), ec);
    return base.string();
}

class EchoCommand : public commands::ICommand {
public:
    std::string command_id() const override { return "test.echo"; }
    std::string title() const override { return "Echo"; }
    std::string category() const override { return "Test"; }
    std::vector<std::string> keywords() const override { return {"echo"}; }
    core::Result<core::Json> execute(const core::Json& arguments) override {
        return core::Result<core::Json>::ok(arguments);
    }
};

class ThrowingCommand : public commands::ICommand {
public:
    std::string command_id() const override { return "test.throw"; }
    std::string title() const override { return "Throw"; }
    std::string category() const override { return "Test"; }
    core::Result<core::Json> execute(const core::Json&) override {
        throw core::TrinityException(core::Error(core::ErrorCode::EngineExecutionError,
                                                 "intentional test failure"));
    }
};

}  // namespace

TEST_CASE("EventBus matches exact, prefix-wildcard and catch-all topics") {
    core::EventBus bus;
    int exact = 0;
    int wildcard = 0;
    int everything = 0;
    bus.subscribe("job.finished", [&](const core::Event&) { ++exact; });
    bus.subscribe("job.*", [&](const core::Event&) { ++wildcard; });
    bus.subscribe("*", [&](const core::Event&) { ++everything; });

    CHECK(bus.publish("job.finished") == 3);
    CHECK(bus.publish("job.log") == 2);
    CHECK(bus.publish("app.started") == 1);

    CHECK(exact == 1);
    CHECK(wildcard == 2);
    CHECK(everything == 3);

    CHECK(core::EventBus::topic_matches("job.*", "job.progress"));
    CHECK_FALSE(core::EventBus::topic_matches("job.*", "jobs"));
    CHECK_FALSE(core::EventBus::topic_matches("job.*", "job"));
    CHECK(core::EventBus::topic_matches("*", "anything.at.all"));
}

TEST_CASE("EventBus carries structured payloads and unsubscribes cleanly") {
    core::EventBus bus;
    core::Json received = core::Json::object();
    const core::EventSubscriptionId id = bus.subscribe("job.finished", [&](const core::Event& event) {
        received = event.payload;
    });

    core::Json payload = core::Json::object();
    payload["job_id"] = "abc";
    payload["status"] = "COMPLETED";
    CHECK(bus.publish("job.finished", payload) == 1);

    CHECK(received.find("job_id")->as_string() == "abc");
    CHECK(received.find("status")->as_string() == "COMPLETED");
    CHECK(bus.subscriber_count() == 1);

    CHECK(bus.unsubscribe(id));
    CHECK(bus.subscriber_count() == 0);
    CHECK(bus.publish("job.finished") == 0);
    CHECK_FALSE(bus.unsubscribe(id));
}

TEST_CASE("EventBus contains a throwing handler and keeps the replay ring") {
    core::EventBus bus;
    bool healthy_handler_ran = false;
    bus.subscribe("*", [](const core::Event&) { throw std::runtime_error("handler exploded"); });
    bus.subscribe("job.log", [&](const core::Event&) { healthy_handler_ran = true; });

    // Only the healthy handler counts as delivered; the thrower is logged.
    CHECK(bus.publish("job.log") == 1);
    CHECK(healthy_handler_ran);

    bus.publish("job.log");
    const std::vector<core::Event> recent = bus.recent(10);
    REQUIRE(recent.size() == 2);
    CHECK(recent.at(0).topic == "job.log");
    CHECK_FALSE(recent.at(0).timestamp.empty());
    CHECK(bus.published_count() == 2);

    bus.clear();
    CHECK(bus.recent(10).empty());
}

TEST_CASE("EventBus is safe under concurrent publishing") {
    core::EventBus bus;
    std::atomic<int> delivered{0};
    bus.subscribe("job.*", [&](const core::Event&) { delivered.fetch_add(1); });

    std::vector<std::thread> publishers;
    for (int thread_index = 0; thread_index < 4; ++thread_index) {
        publishers.emplace_back([&bus] {
            for (int i = 0; i < 25; ++i) bus.publish("job.progress");
        });
    }
    for (std::thread& publisher : publishers) publisher.join();

    CHECK(delivered.load() == 100);
    CHECK(bus.published_count() == 100);
}

TEST_CASE("Plugin manifests are validated") {
    auto rejected = plugins::PluginManifest::from_json(core::Json::parse(R"({"name":"x"})"));
    CHECK(rejected.is_error());
    CHECK(rejected.error().code() == core::ErrorCode::RequestValidationError);

    auto bad_id = plugins::PluginManifest::from_json(
        core::Json::parse(R"({"plugin_id":"Bad Id!","name":"x","version":"1"})"));
    CHECK(bad_id.is_error());

    auto accepted = plugins::PluginManifest::from_json(core::Json::parse(
        R"({"plugin_id":"trinity.sample","name":"Sample","version":"1.2.3",
            "capabilities":["cad.preview","math.evaluate"]})"));
    REQUIRE(accepted.is_ok());
    CHECK(accepted.value().plugin_id == "trinity.sample");
    CHECK(accepted.value().capabilities.size() == 2);
    CHECK(accepted.value().to_json().find("capabilities") != nullptr);
}

TEST_CASE("PluginManager discovers manifests and manages lifecycle") {
    const std::string directory = fresh_temp_dir("plugins");
    std::error_code ec;
    REQUIRE(core::FileSystem::write_file_atomic(
        directory + "/sample.plugin.json",
        R"({"plugin_id":"trinity.sample","name":"Sample","version":"1.0.0"})", ec));
    REQUIRE(core::FileSystem::write_file_atomic(
        directory + "/broken.plugin.json", R"({"plugin_id":"trinity.broken"})", ec));
    REQUIRE(core::FileSystem::write_file_atomic(directory + "/notes.txt", "not a manifest", ec));

    core::EventBus bus;
    int changes = 0;
    bus.subscribe(core::topics::kPluginChanged, [&](const core::Event&) { ++changes; });

    plugins::PluginManager manager(&bus);
    auto discovered = manager.discover(directory);
    REQUIRE(discovered.is_ok());
    CHECK(discovered.value() == 1);  // the broken manifest is rejected, the .txt ignored
    CHECK(manager.count() == 1);

    auto loaded = manager.load_all();
    REQUIRE(loaded.is_ok());
    CHECK(loaded.value() == 1);
    CHECK(manager.loaded_count() == 1);
    CHECK(changes == 1);

    auto record = manager.get("trinity.sample");
    REQUIRE(record.is_ok());
    CHECK(record.value().state == plugins::PluginState::Loaded);

    // Registering the same id twice is rejected.
    auto duplicate = std::make_shared<plugins::ManifestPlugin>(
        plugins::PluginManifest::from_json(core::Json::parse(
            R"({"plugin_id":"trinity.sample","name":"Sample","version":"1.0.0"})"))
            .value());
    CHECK(manager.register_plugin(duplicate).is_error());

    CHECK(manager.unload("trinity.sample").is_ok());
    CHECK(manager.loaded_count() == 0);
    manager.unload_all();
    CHECK(manager.status().find("plugins") != nullptr);

    core::FileSystem::remove_all(directory, ec);
}

TEST_CASE("CommandRegistry registers, executes and contains failures") {
    core::EventBus bus;
    int executed_events = 0;
    bus.subscribe(core::topics::kCommandExecuted, [&](const core::Event&) { ++executed_events; });

    commands::CommandRegistry registry(&bus);
    REQUIRE(registry.register_command(std::make_shared<EchoCommand>()).is_ok());
    REQUIRE(registry.register_command(std::make_shared<ThrowingCommand>()).is_ok());
    CHECK(registry.count() == 2);

    // Duplicate ids are rejected.
    CHECK(registry.register_command(std::make_shared<EchoCommand>()).is_error());

    core::Json arguments = core::Json::object();
    arguments["text"] = "hello";
    auto echoed = registry.execute("test.echo", arguments);
    REQUIRE(echoed.is_ok());
    CHECK(echoed.value().find("text")->as_string() == "hello");

    // A throwing command becomes a classified error, not a crash.
    auto thrown = registry.execute("test.throw");
    CHECK(thrown.is_error());
    CHECK(thrown.error().code() == core::ErrorCode::EngineExecutionError);

    // Unknown ids are rejected without touching any command.
    auto unknown = registry.execute("test.does_not_exist");
    CHECK(unknown.is_error());

    CHECK(executed_events == 3);

    const std::vector<core::Json> catalogue = registry.catalogue();
    REQUIRE(catalogue.size() == 2);
    CHECK(catalogue.at(0).find("command_id") != nullptr);
    CHECK(catalogue.at(0).find("keywords") != nullptr);
}
