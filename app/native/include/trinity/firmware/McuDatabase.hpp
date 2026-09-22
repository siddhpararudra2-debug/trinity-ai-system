#pragma once

// Manually defined MCU database: the only supported hardware.
// Capabilities here are facts about real chips, not guesses —
// anything absent (pin, peripheral, clock above max) is rejected.

#include <string>
#include <vector>

#include "FirmwareProject.hpp"

namespace trinity::firmware {

/// Supported model names ("ESP32", "STM32F401RE", "RP2040").
std::vector<std::string> supportedMcus();

/// Full MCU record; throws RequestValidationError for unknown models.
MCU lookupMcu(const std::string& model);

}  // namespace trinity::firmware
