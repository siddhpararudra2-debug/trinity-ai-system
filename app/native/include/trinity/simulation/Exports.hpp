#pragma once

#include <string>

#include "Types.hpp"

namespace trinity::simulation {

void writeCsv(const std::string& path, const SimulationResult& result);

/// Structured JSON export: parameters, initial conditions, integration
/// method, time step, duration, result metadata, validation state, and
/// artifact references. Large sample series stay in the CSV; this file
/// carries a bounded sample window for convenience only.
void writeJson(const std::string& path, const SimulationProject& project,
               const SimulationResult& result, const core::Json& validation,
               const core::Json& artifactReferences = core::Json::array());

}  // namespace trinity::simulation
