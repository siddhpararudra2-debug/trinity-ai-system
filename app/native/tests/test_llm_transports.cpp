#include <doctest.h>

#include <string>
#include <vector>

#include "trinity/intelligence/HttpTransport.hpp"
#include "trinity/intelligence/providers/AnthropicProvider.hpp"
#include "trinity/intelligence/providers/LocalModelProvider.hpp"
#include "trinity/intelligence/providers/OpenAIProvider.hpp"

using namespace trinity::intelligence;
using namespace trinity::intelligence::providers;

namespace {

// Deterministic contract surface: every provider test injects this instead
// of DefaultHttpTransport. It records requests so bodies/headers can be
// asserted, and replays canned replies in order.
class FixtureTransport : public IHttpTransport {
public:
    std::vector<HttpRequest> requests;
    std::vector<HttpReply> replies;

    struct ScriptedStream {
        std::vector<std::string> chunks;
        HttpReply reply;
    };
    std::vector<ScriptedStream> streams;

    HttpReply post(const HttpRequest& request) override {
        requests.push_back(request);
        if (replies.empty()) {
            HttpReply exhausted;
            exhausted.errorText = "fixture ran out of canned replies";
            return exhausted;
        }
        HttpReply reply = replies.front();
        replies.erase(replies.begin());
        return reply;
    }

    HttpReply postStream(const HttpRequest& request,
                         const BodyChunkCallback& onChunk) override {
        if (streams.empty()) {
            // Legacy shape: still works through the base-class default —
            // post() plus the entire body as a single chunk.
            return IHttpTransport::postStream(request, onChunk);
        }
        requests.push_back(request);
        ScriptedStream stream = streams.front();
        streams.erase(streams.begin());
        if (onChunk) {
            for (const auto& chunk : stream.chunks) {
                onChunk(chunk);
            }
        }
        return stream.reply;
    }
};

HttpReply httpReply(long status, const std::string& body) {
    HttpReply reply;
    reply.transportOk = true;
    reply.statusCode = status;
    reply.body = body;
    return reply;
}

ModelRequest sampleRequest() {
    ModelRequest request;
    request.requestId = "req-tests-1";
    request.prompt = "Create a 50 mm quadcopter frame STL";
    Message systemMessage;
    systemMessage.role = MessageRole::System;
    systemMessage.content = "Be concise.";
    request.messages.push_back(systemMessage);
    return request;
}

const std::string kOpenAiToolReply =
    R"({"id":"chatcmpl-1","object":"chat.completion","model":"gpt-4o",
        "usage":{"prompt_tokens":10,"completion_tokens":5,"total_tokens":15},
        "choices":[{"index":0,"finish_reason":"tool_calls","message":{
        "role":"assistant","content":null,"tool_calls":[
        {"id":"call_a1","type":"function","function":{"name":"trinity_execute",
         "arguments":"{\"engine\":\"cad\",\"operation\":\"generate\",\"parameters\":{\"type\":\"quadcopter_frame\"}}"}},
        {"id":"call_b2","type":"function","function":{"name":"trinity_execute",
         "arguments":"{\"engine\":\"math\",\"operation\":\"evaluate\",\"parameters\":{\"expression\":\"2+2\"}}"}}
        ]}}]})";

const std::string kAnthropicToolReply =
    R"({"id":"msg_1","type":"message","role":"assistant","model":"claude-sonnet-4-20250514",
        "usage":{"input_tokens":7,"output_tokens":9},
        "content":[
        {"type":"text","text":"Planning a CAD build."},
        {"type":"tool_use","id":"toolu_1","name":"trinity_execute",
         "input":{"engine":"cad","operation":"generate","parameters":{"type":"quadcopter_frame"}}},
        {"type":"tool_use","id":"toolu_2","name":"evil_execute",
         "input":{"engine":"cad","operation":"generate","parameters":{}}}
        ]})";

}  // namespace

TEST_CASE("refusal before any network when a key is required and absent") {
    OpenAIProvider provider;
    const ModelResponse response = provider.generate(sampleRequest());
    CHECK_FALSE(response.success);
    CHECK(response.error["code"] == "capability_unavailable");

    const ModelProviderInfo disabledInfo = provider.info();
    CHECK_FALSE(disabledInfo.available);
    CHECK(disabledInfo.endpoint == "https://api.openai.com/v1/chat/completions");
}

TEST_CASE("openai tool-call round trip maps into ToolCall payloads") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(httpReply(200, kOpenAiToolReply));
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());
    CHECK(provider.info().available);

    const trinity::core::Result<ModelResponse> plan = provider.generatePlan(sampleRequest());
    REQUIRE(plan.isOk());
    const ModelResponse response = plan.value();
    CHECK(response.success);
    REQUIRE(response.toolCalls.size() == 2);
    CHECK(response.hasToolCall);
    CHECK(response.toolCall.engine == "cad");
    CHECK(response.toolCall.operation == "generate");
    CHECK(response.toolCall.parameters["type"] == "quadcopter_frame");
    CHECK(response.toolCalls[0].toolCallId == "call_a1");
    CHECK(response.toolCalls[1].toolCallId == "call_b2");
    CHECK(response.usage["completion_tokens"] == 5);
    CHECK(response.responseId == "chatcmpl-1");

    REQUIRE(fixture->requests.size() == 1);
    const HttpRequest& sent = fixture->requests.front();
    CHECK(sent.url == "https://api.openai.com/v1/chat/completions");
    bool sawBearer = false;
    for (const auto& header : sent.header) {
        if (header.first == "Authorization" && header.second == "Bearer TESTKEY") {
            sawBearer = true;
        }
    }
    CHECK(sawBearer);
    const trinity::core::Json body = trinity::core::Json::parse(sent.body);
    CHECK(body["model"] == "gpt-4o");
    CHECK(body["tool_choice"] == "auto");
    CHECK(body["tools"][0]["function"]["name"] == "trinity_execute");
    REQUIRE(body["messages"].size() >= 2);
    CHECK(body["messages"][0]["role"] == "system");
}

TEST_CASE("openai text-only reply fills text without tool calls") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(httpReply(
        200,
        R"({"id":"chatcmpl-2","model":"gpt-4o","choices":[
            {"index":0,"finish_reason":"stop",
             "message":{"role":"assistant","content":"This is a plain answer."}}]})"));
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    const ModelResponse response = provider.generate(sampleRequest());
    CHECK(response.success);
    CHECK(response.text == "This is a plain answer.");
    CHECK_FALSE(response.hasToolCall);
}

TEST_CASE("anthropic round trip maps tool_use blocks and rejects unknown tools") {
    AnthropicProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(httpReply(200, kAnthropicToolReply));
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());
    CHECK(provider.info().available);

    const ModelResponse response = provider.generate(sampleRequest());
    CHECK(response.success);
    CHECK(response.text == "Planning a CAD build.");
    // evil_execute is not the canonical tool and must never execute.
    CHECK(response.toolCalls.size() == 1);
    CHECK(response.toolCall.engine == "cad");
    CHECK(response.toolCall.operation == "generate");
    CHECK(response.toolCall.toolCallId == "toolu_1");
    CHECK(response.usage["input_tokens"] == 7);

    REQUIRE(fixture->requests.size() == 1);
    const HttpRequest& sent = fixture->requests.front();
    CHECK(sent.url == "https://api.anthropic.com/v1/messages");
    bool sawApiKey = false;
    bool sawVersion = false;
    for (const auto& header : sent.header) {
        if (header.first == "x-api-key" && header.second == "TESTKEY") {
            sawApiKey = true;
        }
        if (header.first == "anthropic-version") {
            sawVersion = true;
        }
    }
    CHECK(sawApiKey);
    CHECK(sawVersion);
    // Anthropic carries API version/keys in headers only — no bearer body.
    const trinity::core::Json body = trinity::core::Json::parse(sent.body);
    CHECK(body.contains("system"));
    CHECK(body["messages"][0]["role"] == "user");
    CHECK(body["tools"][0]["name"] == "trinity_execute");
}

TEST_CASE("provider HTTP errors surface as structured refusals, never exceptions") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(
        httpReply(429, R"({"error":{"message":"rate limited","type":"stripe-ish"}})"));
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    const trinity::core::Result<ModelResponse> plan = provider.generatePlan(sampleRequest());
    REQUIRE(plan.isOk());
    const ModelResponse response = plan.value();
    CHECK_FALSE(response.success);
    CHECK(response.error["message"] == "rate limited");
}

TEST_CASE("transport failure is also a refusal, not a crash") {
    AnthropicProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    const ModelResponse response = provider.generate(sampleRequest());
    CHECK_FALSE(response.success);
    CHECK(response.error.contains("message"));
    // Availability is a config-level statement: key present + transport
    // injected, so info().available can be true while this round trip failed
    // at the network layer.
}

TEST_CASE("local provider runs keyless over the OpenAI-compatible endpoint") {
    LocalModelProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(httpReply(200, kOpenAiToolReply));
    provider.setTransport(fixture);
    CHECK(provider.info().configured);  // keyless by design
    CHECK(provider.info().available);

    const ModelResponse response = provider.generate(sampleRequest());
    CHECK(response.success);
    CHECK(response.toolCalls.size() == 2);
    CHECK(fixture->requests.front().url == "http://localhost:11434/v1/chat/completions");
}

TEST_CASE("streamPlan emits exactly one final chunk") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    fixture->replies.push_back(httpReply(200, kOpenAiToolReply));
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    int finals = 0;
    std::string payload;
    provider.streamPlan(sampleRequest(), [&](const std::string& chunk, bool final) {
        if (final) {
            ++finals;
            payload = chunk;
        }
    });
    CHECK(finals == 1);
    CHECK(payload.find("cad") != std::string::npos);
    // The streaming request must actually ask for SSE.
    const trinity::core::Json body = trinity::core::Json::parse(fixture->requests.front().body);
    CHECK(body["stream"] == true);
}

TEST_CASE("openai SSE deltas stream as non-final chunks and finish as one payload") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    FixtureTransport::ScriptedStream stream;
    // Text delta, then tool call split across chunks (arguments arrive as
    // fragments), then [DONE].
    stream.chunks = {
        "data: {\"id\":\"chatcmpl-9\",\"model\":\"gpt-4o\",\"choices\":[{\"delta\":"
        "{\"content\":\"Planning \"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"a frame.\"}}]}\n\n",
        // Tool call: name chunk, then arguments fragmenting across a chunk
        // boundary — buffers must line-split safely mid-JSON.
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"id\":\"call_a1\",\"function\":"
        "{\"name\":\"trinity_execute\"}}]}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"tool_calls\":[{\"function\":{\"arguments\":"
        "\"{\\\"engine\\\":\\\"cad\\\",\\\"oper",
        "ation\\\":\\\"generate\\\",\\\"parameters\\\":{\\\"type\\\":\\\"quadcopter_frame\\\"}}"
        "\"}}]}}]}\n\n",
        "data: [DONE]\n\n",
    };
    FixtureTransport::ScriptedStream copy = stream;  // reply reused verbatim
    copy.reply.transportOk = true;
    copy.reply.statusCode = 200;
    fixture->streams.push_back(std::move(copy));

    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    int finals = 0;
    std::string streamed;
    std::string payload;
    provider.streamPlan(sampleRequest(), [&](const std::string& chunk, bool final) {
        if (final) {
            ++finals;
            payload = chunk;
        } else {
            streamed += chunk;
        }
    });
    CHECK(finals == 1);
    CHECK(streamed == "Planning a frame.");
    // Final payload is the accumulated step list, not the streamed prefix.
    MESSAGE("DBG openai payload=", payload);
    const trinity::core::Json steps = trinity::core::Json::parse(payload);
    REQUIRE(steps.is_array());
    REQUIRE(steps.size() == 1);
    CHECK(steps[0]["engine"] == "cad");
    CHECK(steps[0]["operation"] == "generate");
    CHECK(steps[0]["tool_call_id"] == "call_a1");
}

TEST_CASE("anthropic SSE text + tool_use deltas accumulate into one final payload") {
    AnthropicProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    FixtureTransport::ScriptedStream stream;
    stream.chunks = {
        "event: message_start\ndata: {\"type\":\"message_start\",\"message\":{\"id\":\"msg_9\","
        "\"model\":\"claude-sonnet-4-20250514\",\"usage\":{\"input_tokens\":7,\"output_tokens\":9}}}\n\n",
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\"}}\n\n",
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Sketching \"}}\n\n",
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"the frame.\"}}\n\n",
        "event: content_block_start\ndata: {\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"trinity_execute\"}}\n\n",
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"engine\\\":\\\"cad\\\"\"}}\n\n",
        "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\",\\\"operation\\\":\\\"generate\\\","
        "\\\"parameters\\\":{\\\"type\\\":\\\"quadcopter_frame\\\"}}\"}}\n\n",
        "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n",
    };
    stream.reply.transportOk = true;
    stream.reply.statusCode = 200;
    fixture->streams.push_back(std::move(stream));

    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    int finals = 0;
    std::string streamed;
    std::string payload;
    provider.streamPlan(sampleRequest(), [&](const std::string& chunk, bool final) {
        if (final) {
            ++finals;
            payload = chunk;
        } else {
            streamed += chunk;
        }
    });
    CHECK(finals == 1);
    CHECK(streamed == "Sketching the frame.");
    MESSAGE("DBG anthropic streamed=", streamed, " payload=", payload);
    const trinity::core::Json steps = trinity::core::Json::parse(payload);
    REQUIRE(steps.is_array());
    REQUIRE(steps.size() == 1);
    CHECK(steps[0]["engine"] == "cad");
    CHECK(steps[0]["tool_call_id"] == "toolu_1");
}

TEST_CASE("SSE error events surface truthfully as the final chunk") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    FixtureTransport::ScriptedStream stream;
    stream.chunks = {
        "data: {\"error\":{\"message\":\"operator overloaded\"}}\n\n",
        "data: [DONE]\n\n",
    };
    stream.reply.errorText = "plain failure";  // reply must not mask the SSE error
    stream.reply.statusCode = 200;
    stream.reply.transportOk = true;
    fixture->streams.push_back(std::move(stream));

    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}}).isOk());

    int finals = 0;
    std::string payload;
    provider.streamPlan(sampleRequest(), [&](const std::string& chunk, bool final) {
        if (final) {
            ++finals;
            payload = chunk;
        }
    });
    CHECK(finals == 1);
    CHECK(payload.find("operator overloaded") != std::string::npos);
}

TEST_CASE("disabled provider refuses without network") {
    OpenAIProvider provider;
    auto fixture = std::make_shared<FixtureTransport>();
    provider.setTransport(fixture);
    REQUIRE(provider.configure({{"api_key", "TESTKEY"}, {"enabled", false}}).isOk());
    CHECK_FALSE(provider.info().available);

    const ModelResponse response = provider.generate(sampleRequest());
    CHECK_FALSE(response.success);
    CHECK(fixture->requests.empty());
}

TEST_CASE("default transport is unavailable off-Windows and truthful on it") {
    DefaultHttpTransport transport;
    HttpRequest request;
    request.url = "https://api.example.test/v1/none";
    const HttpReply reply = transport.post(request);
#ifndef _WIN32
    CHECK_FALSE(reply.transportOk);
    CHECK_FALSE(reply.errorText.empty());
#else
    // A real WinHTTP round trip against an unused domain fails at connect or
    // DNS; either way the transport must report honestly, not nonsense.
    CHECK_FALSE(reply.transportOk);
    CHECK_FALSE(reply.errorText.empty());
#endif
}

TEST_CASE("live round trip only when a key is configured in the environment") {
    if (::getenv("TRINITY_LLM_API_KEY") == nullptr ||
        std::string(::getenv("TRINITY_LLM_API_KEY")).empty()) {
        MESSAGE("skipping live LLM test: TRINITY_LLM_API_KEY not set (no-key CI)");
        return;
    }
    const char* providerName = ::getenv("TRINITY_LLM_PROVIDER");
    const std::string which = providerName != nullptr ? std::string(providerName) : "openai";
    std::shared_ptr<IModelProvider> provider;
    if (which == "anthropic") {
        provider = std::make_shared<AnthropicProvider>();
    } else {
        provider = std::make_shared<OpenAIProvider>();
    }
    CHECK(provider->configure({}).isOk());
    if (!provider->info().available) {
        MESSAGE("skipping live LLM test: provider not configured");
        return;
    }
    const ModelResponse response = provider->generate(sampleRequest());
    // Either a working answer or a structured refusal — never a crash.
    const bool answered = response.success;
    const bool refused = response.error.contains("message");
    CHECK((answered || refused));
}
