#pragma once

// Deterministic integrators. Kinematics models use closed-form
// solutions; basic_dynamics (constant F, m) uses semi-implicit Euler or
// RK4 (method selects); multi_body spring chains integrate the coupled
// Hooke system with the selected method. Cancel probes are polled
// every kCancelCheckInterval steps.

#include "Types.hpp"

namespace trinity::simulation {

struct IntegrateOutcome {
    bool success = false;
    bool cancelled = false;
    std::string error;
    SimulationResult result;
};

IntegrateOutcome integrate(const SimulationProject& project,
                           const CancelProbe& cancel = nullptr);

}  // namespace trinity::simulation
