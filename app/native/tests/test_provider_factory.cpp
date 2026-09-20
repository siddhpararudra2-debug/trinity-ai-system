#include <doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "trinity/core/Config.hpp"
#include "trinity/intelligence/ExampleProvider.hpp"
#include "trinity/intelligence/ModelProviderFactory.hpp"

#ifdef _WIN32
#define TRINITY_SETENV(name, value) _putenv_s(name, value)
#else
#define TRINITY_SETENV(name, value) setenv(name, value, 1)
#endif

namespace {

class EchoTestProvider : public trinity::intelligence::IModelProvider {
public:
    trinity::intelligence::ModelProviderInfo info() const override {
        trinity::intelligence::ModelProviderInfo info;
        info.providerId = "echo-test";
        info.displayName = "Echo test provider";
        info.available = true;
        info.configured = true;
        return info;
    }

    trinity::intelligence::ModelResponse generate(
        const trinity::intelligence::ModelRequest& request) override {
        trinity::intelligence::ModelResponse response;
        response.success = true;
        response.requestId = request.requestId;
        response.text = "echo";
        return response;
    }

    trinity::core::Result<trinity::intelligence::ModelResponse> generatePlan(
        const trinity::intelligence::ModelRequest& request) override {
        return trinity::core::Result<trinity::intelligence::ModelResponse>::ok(
            generate(request));
    }

    void streamPlan(const trinity::intelligence::ModelRequest&,
                    StreamCallback callback) override {
        callback("echo", true);
    }

    trinity::core::Status configure(const trinity::core::Json&) override {
        return trinity::core::okStatus();
    }
};

}  // namespace

TEST_CASE("factory ships null and lists providers") {
    auto& factory = trinity::intelligence::ModelProviderFactory::instance();
    CHECK(factory.has("null"));
    bool sawNull = false;
    for (const auto& id : factory.list()) {
        if (id == "null") {
            sawNull = true;
        }
    }
    CHECK(sawNull);
}

TEST_CASE("factory registers, creates, and rejects duplicates") {
    auto& factory = trinity::intelligence::ModelProviderFactory::instance();
    if (!factory.has("echo-test")) {
        factory.registerProvider("echo-test", [](const trinity::core::Json&) {
            return std::make_shared<EchoTestProvider>();
        });
    }
    CHECK(factory.has("echo-test"));
    CHECK_THROWS_AS(
        factory.registerProvider("echo-test", [](const trinity::core::Json&) {
            return std::make_shared<EchoTestProvider>();
        }),
        trinity::core::RequestValidationError);
    CHECK_THROWS_AS(factory.registerProvider("", [](const trinity::core::Json&) {
        return std::make_shared<EchoTestProvider>();
    }),
                    trinity::core::RequestValidationError);

    auto provider = factory.create("echo-test", trinity::core::Json::object());
    CHECK(provider->info().available);
}

TEST_CASE("factory unknown id names the provider and available set") {
    auto& factory = trinity::intelligence::ModelProviderFactory::instance();
    try {
        factory.create("ghost-llm-xyz", trinity::core::Json::object());
        FAIL("expected RequestValidationError");
    } catch (const trinity::core::RequestValidationError& err) {
        CHECK(err.toJson()["code"] == "request_validation_error");
        CHECK(err.toJson()["details"]["provider"] == "ghost-llm-xyz");
        CHECK(err.toJson()["details"].contains("available"));
    }
    // createOrNull never throws: typos fall back to null so boot survives.
    const auto fallback =
        factory.createOrNull("ghost-llm-xyz", trinity::core::Json::object());
    CHECK_FALSE(fallback->info().available);
    CHECK(fallback->info().providerId == "null");
}

TEST_CASE("example provider compiles and refuses truthfully") {
    trinity::intelligence::ExampleProvider example;
    CHECK_FALSE(example.info().available);
    trinity::intelligence::ModelRequest request;
    request.prompt = "hello";
    const auto response = example.generate(request);
    CHECK_FALSE(response.success);
    CHECK(response.error.value("code", "") == "capability_unavailable");
    // Deliberately NOT registered: the dev must copy + register their own.
    CHECK_FALSE(trinity::intelligence::ModelProviderFactory::instance().has("example"));
}

TEST_CASE("model settings come from env and never carry secrets") {
    const std::string root =
        (std::filesystem::temp_directory_path() / "trinity-test-modelcfg").string();
    TRINITY_SETENV("TRINITY_STORAGE_ROOT", root.c_str());
    TRINITY_SETENV("TRINITY_MODEL_PROVIDER", "mine");
    TRINITY_SETENV("TRINITY_MODEL_ENDPOINT", "https://llm.example/v1");
    TRINITY_SETENV("TRINITY_MODEL_NAME", "trinity-1");
    TRINITY_SETENV("TRINITY_MODEL_API_KEY", "super-secret-key");

    const trinity::core::Settings settings = trinity::core::loadSettings();
    CHECK(settings.model.providerId == "mine");
    CHECK(settings.model.endpoint == "https://llm.example/v1");
    CHECK(settings.model.modelName == "trinity-1");
    const std::string dumped = settings.toJson().dump();
    CHECK(dumped.find("super-secret-key") == std::string::npos);
    CHECK(dumped.find("api_key") == std::string::npos);
    CHECK(trinity::core::modelApiKeyFromEnv() == "super-secret-key");

    TRINITY_SETENV("TRINITY_STORAGE_ROOT", "");
    TRINITY_SETENV("TRINITY_MODEL_PROVIDER", "");
    TRINITY_SETENV("TRINITY_MODEL_ENDPOINT", "");
    TRINITY_SETENV("TRINITY_MODEL_NAME", "");
    TRINITY_SETENV("TRINITY_MODEL_API_KEY", "");
    const trinity::core::Settings defaults = trinity::core::loadSettings();
    CHECK(defaults.model.providerId == "null");
}
