#pragma once

// PCB design validation over the tool-agnostic IR. Returns a standard
// ValidationResult: VALIDATED when every rule passes, INVALID otherwise.
// VERIFIED is never claimed — that state needs measured/checked output
// against fabricated hardware, which this phase does not do.

#include "../validation/ValidationResult.hpp"
#include "PcbDesign.hpp"

namespace trinity::pcb {

validation::ValidationResult validatePcbDesign(const PcbDesign& design);

}  // namespace trinity::pcb
