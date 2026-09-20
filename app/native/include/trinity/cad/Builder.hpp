#pragma once

// Quadcopter frame builder: center plate + 4 arms + 4 motor bosses in
// X configuration (ports src/engines/cad/builder.py). Nine boxes,
// 108 triangles, deterministic for a given parameter set.

#include "FrameParams.hpp"
#include "Mesh.hpp"

namespace trinity::cad {

/// Arm angles in degrees (X configuration).
extern const double kArmAnglesDeg[4];

Mesh buildQuadcopterFrame(const FrameParams& params);

}  // namespace trinity::cad
