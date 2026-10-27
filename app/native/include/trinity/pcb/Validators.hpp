#pragma once

// PCB design validation over the tool-agnostic IR. Returns a standard
// ValidationResult: VALIDATED when every rule passes, INVALID otherwise.
// VERIFIED is never claimed — that state needs measured/checked output
// against fabricated hardware, which this phase does not do.

#include "../validation/ValidationResult.hpp"
#include "PcbDesign.hpp"

namespace trinity::pcb {

validation::ValidationResult validatePcbDesign(const PcbDesign& design);

/// Shortest distance in mm between two closed segments (testing seam
/// for the trace clearance evaluator; same routine the DRC uses).
double trackSegmentDistanceMm(double ax1, double ay1, double ax2, double ay2,
                               double bx1, double by1, double bx2, double by2);

}  // namespace trinity::pcb
