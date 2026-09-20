#pragma once

// CAD engine skeleton: capability registration, request validation,
// operation routing, result/validation/artifact contracts. Full
// quadcopter geometry is a later phase; unsupported operations return
// structured CAPABILITY_UNAVAILABLE rather than fake geometry.

#include <string>

#include "Engine.hpp"

namespace trinity::engines {

class CadEngine : public EngineBase {
public:
    CadEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;
};

}  // namespace trinity::engines
