#pragma once

// CAD intermediate representation: Trinity's own parametric model for a
// quadcopter frame, independent of any CAD kernel (ports
// src/engines/cad/ir.py). Units are always millimetres.

#include <map>
#include <string>

#include "../core/Json.hpp"

namespace trinity::cad {

/// Default parameters for a 50 mm quadcopter frame (all millimetres).
const std::map<std::string, double>& defaultFrameParams();

struct FrameParams {
    std::string units = "mm";
    std::map<std::string, double> parameters;

    /// Merge raw overrides over the defaults and enforce V1 engineering
    /// rules. Throws RequestValidationError on unknown keys or geometry
    /// outside the supported range.
    static FrameParams fromRequest(const core::Json& raw);

    core::Json toJson() const;
};

}  // namespace trinity::cad
