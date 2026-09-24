#pragma once

#include <string>

#include "Types.hpp"

namespace trinity::simulation {

void writeCsv(const std::string& path, const SimulationResult& result);
void writeJson(const std::string& path, const SimulationProject& project,
               const SimulationResult& result, const core::Json& validation);

}  // namespace trinity::simulation
