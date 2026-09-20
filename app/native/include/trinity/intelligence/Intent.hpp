#pragma once

// Natural-language requirement as parsed by the deterministic router
// (mirrors src/intelligence/router.py). Only the CAD quadcopter-frame
// pattern is recognized in V1; anything else is a validation error.

#include <string>

#include "../core/Json.hpp"

namespace trinity::intelligence {

struct Intent {
    std::string intentId;
    std::string domain;     // e.g. "cad"
    std::string operation;  // e.g. "generate"
    std::string object;     // e.g. "quadcopter_frame"
    core::Json parameters = core::Json::object();
    std::string units;
    double confidence = 0.0;

    core::Json toJson() const;
    static Intent fromJson(const core::Json& json);
};

}  // namespace trinity::intelligence
