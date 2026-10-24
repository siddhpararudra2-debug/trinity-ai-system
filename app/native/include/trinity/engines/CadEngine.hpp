#pragma once

// CAD engine: parametric part generation over the dependency-free mesh
// backend (ports src/engines/cad/*). `generate` emits binary STL, spec
// JSON, GLB and STEP artifacts; STEP is an ISO-10303-21 faceted B-rep
// (honest tessellated export — analytic B-rep kernels remain the Python
// adapter seam). `boolean` runs real BSP CSG (union/intersect/subtract)
// over part meshes with watertight + volume sanity gates. Unknown
// operations return structured CAPABILITY_UNAVAILABLE rather than fake
// geometry.
//
// Note: STEP here is tessellated; analytic
// B-rep kernels stay behind the Python cad adapters.

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
    EngineResult executeBoolean(const EngineRequest& request);
};

}  // namespace trinity::engines
