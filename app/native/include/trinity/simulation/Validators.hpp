#pragma once

// Project/result validation. VERIFIED is never assigned here without
// finite, monotonic, and closed-form agreement checks.

#include <string>

#include "Types.hpp"

namespace trinity::simulation {

struct ProjectValidation {
    bool ok = false;
    std::string error;
};

struct ResultChecks {
    bool finite = false;
    bool timeMonotonic = false;
    bool closedFormAgreement = false;
    bool ok = false;
    std::string message;
    core::Json details = core::Json::object();
};

long long validateStepCount(double dt, double durationS);
ProjectValidation validateProject(const SimulationProject& project);
ResultChecks validateResult(const SimulationProject& project, const SimulationResult& result);

}  // namespace trinity::simulation
