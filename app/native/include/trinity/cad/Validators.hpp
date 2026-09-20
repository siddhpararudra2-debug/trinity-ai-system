#pragma once

// Quadcopter frame validation (ports src/engines/cad/validators.py):
// dimensions | finiteness/topology | clearances | manufacturability.
// The local backend does box geometry only, so intersection/topology
// checks reduce to finiteness + non-degeneracy — same as Python.

#include <string>

#include "../core/Json.hpp"
#include "FrameParams.hpp"
#include "Mesh.hpp"

namespace trinity::cad {

/// Minimum printable feature size for FDM printing (mm).
constexpr double kMinPrintableFeatureMm = 1.0;

struct FrameValidation {
    bool ok = false;
    core::Json checks = core::Json::object();
};

FrameValidation validateQuadcopterFrame(const FrameParams& params, const Mesh& mesh);

}  // namespace trinity::cad
