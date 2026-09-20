#pragma once

// UUID v4 generation (canonical 8-4-4-4-12 hex form).
// Single UUID utility for jobs, workflows, nodes, artifacts, requests.

#include <string>

namespace trinity::core {

std::string newUuid();
bool isValidUuid(const std::string& value) noexcept;

}  // namespace trinity::core
