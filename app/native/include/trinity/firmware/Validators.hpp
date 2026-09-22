#pragma once

// Firmware project validation over the tool-agnostic IR. Returns a
// standard ValidationResult: VALIDATED when every rule passes, INVALID
// otherwise. VERIFIED is never claimed — that state needs on-hardware
// or simulated proof, which this phase does not produce.

#include "../validation/ValidationResult.hpp"
#include "FirmwareProject.hpp"

namespace trinity::firmware {

validation::ValidationResult validateFirmwareProject(const FirmwareProject& project);

}  // namespace trinity::firmware
