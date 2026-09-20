#pragma once

// CAD engine: parametric quadcopter-frame generation over the
// dependency-free mesh backend (ports src/engines/cad/*). Generates
// binary STL + spec JSON artifacts; STEP and kernel-backed formats
// report CAD_KERNEL_UNAVAILABLE. Unknown operations return structured
// CAPABILITY_UNAVAILABLE rather than fake geometry.

#include <string>

#include "Engine.hpp"

namespace trinity::engines {

class CadEngine : public EngineBase {
public:
    CadEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    EngineResult executeGenerate(const EngineRequest& request);
};

}  // namespace trinity::engines
