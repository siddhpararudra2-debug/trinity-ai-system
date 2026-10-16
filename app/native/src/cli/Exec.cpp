#include "trinity/cli/Exec.hpp"

#include <iostream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "trinity/core/Error.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/engines/StubEngines.hpp"

namespace trinity::cli {
namespace {

core::Json pendingArtifactsJson(const engines::EngineResult& result) {
    core::Json pending = core::Json::array();
    for (const auto& [path, type] : result.pendingArtifacts) {
        pending.push_back(core::Json{{"path", path}, {"type", type}});
    }
    return pending;
}

core::Json envelopeFromResult(engines::EngineResult result) {
    // Python JobManager assigns job_id and promotes pending files.
    result.jobId.clear();
    core::Json json = result.toJson();
    json["job_id"] = "";
    json["pending_artifacts"] = pendingArtifactsJson(result);
    return json;
}

core::Json errorEnvelope(const std::string& engine, const std::string& operation,
                         const core::TrinityError& exc) {
    return core::Json{{"success", false},
                      {"engine", engine},
                      {"operation", operation},
                      {"result", core::Json::object()},
                      {"artifacts", core::Json::array()},
                      {"pending_artifacts", core::Json::array()},
                      {"validation", nullptr},
                      {"errors", core::Json::array({exc.toJson()})},
                      {"job_id", ""}};
}

core::Json parseErrorEnvelope(const std::string& message) {
    core::RequestValidationError exc(message, {{"source", "exec"}}, "cli");
    return errorEnvelope("", "", exc);
}

}  // namespace

engines::EngineResult executeRequest(const engines::EngineRequest& request) {
    engines::EngineRegistry registry;
    engines::registerAllEngines(registry);
    return registry.execute(request);
}

core::Json executeRequestJson(const std::string& input) {
    core::Json parsed;
    try {
        parsed = core::Json::parse(input);
    } catch (const core::Json::parse_error& exc) {
        return parseErrorEnvelope(std::string("Invalid JSON on stdin: ") + exc.what());
    } catch (const std::exception& exc) {
        return parseErrorEnvelope(std::string("Invalid JSON on stdin: ") + exc.what());
    }
    if (!parsed.is_object()) {
        return parseErrorEnvelope("Exec request must be a JSON object");
    }

    engines::EngineRequest request = engines::EngineRequest::fromJson(parsed);
    try {
        return envelopeFromResult(executeRequest(request));
    } catch (const core::TrinityError& exc) {
        return errorEnvelope(request.engine, request.operation, exc);
    } catch (const std::exception& exc) {
        core::EngineExecutionError wrapped(exc.what(), {{"engine", request.engine}},
                                           "cli");
        return errorEnvelope(request.engine, request.operation, wrapped);
    }
}

int runExecFromStdin() {
#ifdef _WIN32
    if (_fileno(stdin) >= 0) {
        _setmode(_fileno(stdin), _O_BINARY);
    }
    if (_fileno(stdout) >= 0) {
        _setmode(_fileno(stdout), _O_BINARY);
    }
#endif
    std::string input((std::istreambuf_iterator<char>(std::cin)),
                      std::istreambuf_iterator<char>());
    if (input.find_first_not_of(" \t\r\n") == std::string::npos) {
        const core::Json envelope =
            parseErrorEnvelope("Empty stdin; expected an EngineRequest JSON object");
        std::cout << envelope.dump() << "\n";
        return 1;
    }
    const core::Json envelope = executeRequestJson(input);
    std::cout << envelope.dump() << "\n";
    std::cout.flush();
    return 0;
}

}  // namespace trinity::cli
