#pragma once

// First real PCB engine: deterministic board/component/net/placement
// modeling over the tool-agnostic IR, with KiCad 7/8 export and
// ArtifactManager integration. State flows as design JSON between
// operations so jobs and workflow nodes chain
// (create -> add -> place -> validate -> export). Anything beyond the
// listed capabilities (routing, ERC/DRC, simulation, footprint
// synthesis) refuses truthfully with structured CAPABILITY_UNAVAILABLE.

#include <string>

#include "Engine.hpp"

namespace trinity::engines {

class PcbEngine : public EngineBase {
public:
    PcbEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

private:
    EngineResult executeCreateBoard(const EngineRequest& request);
    EngineResult executeAddComponent(const EngineRequest& request);
    EngineResult executeAddNet(const EngineRequest& request);
    EngineResult executePlaceComponent(const EngineRequest& request);
    EngineResult executeValidateDesign(const EngineRequest& request);
    EngineResult executeExport(const EngineRequest& request);
};

}  // namespace trinity::engines
