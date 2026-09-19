#include <doctest.h>

#include "trinity/core/Error.hpp"

TEST_CASE("error codes have stable wire names") {
    CHECK(std::string(trinity::core::errorCodeName(
              trinity::core::ErrorCode::RequestValidationError)) ==
          "request_validation_error");
    CHECK(std::string(trinity::core::errorCodeName(
              trinity::core::ErrorCode::EngineNotFoundError)) == "engine_not_found");
    CHECK(std::string(trinity::core::errorCodeName(
              trinity::core::ErrorCode::CapabilityUnavailableError)) ==
          "capability_unavailable");
}

TEST_CASE("errors serialize to the shared envelope") {
    trinity::core::EngineNotFoundError err("missing", trinity::core::Json{{"a", 1}});
    const trinity::core::Json json = err.toJson();
    CHECK(json["code"] == "engine_not_found");
    CHECK(json["message"] == "missing");
    CHECK(json["details"]["a"] == 1);
    CHECK(std::string(err.what()) == "missing");
}
