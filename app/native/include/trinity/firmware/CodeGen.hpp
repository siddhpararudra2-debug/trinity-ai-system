#pragma once

// Deterministic Arduino-style source generation from the firmware IR.
// Emits main.cpp (setup/loop), config.h (pin/peripheral defines) and
// platform.h (MCU guards). Output is a pure function of the project:
// different valid configurations produce corresponding source.

#include <string>
#include <vector>

#include "FirmwareProject.hpp"

namespace trinity::firmware {

/// Render all three sources for a project with an MCU selected.
/// Throws RequestValidationError when generation preconditions fail.
std::vector<SourceFile> generateSources(const FirmwareProject& project);

}  // namespace trinity::firmware
