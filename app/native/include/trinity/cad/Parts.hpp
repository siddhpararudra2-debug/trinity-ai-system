#pragma once

// Multi-part CAD registry: ports src/engines/cad/parts.py.
//
// Each supported part type is a PartDefinition — defaults, bounds,
// cross-field rules, builder, validator and filename stem — so the engine
// dispatches through the registry instead of hardcoding one part's
// callables. Adding a part is a registration, not an edit to the engine.
// Units are always millimetres.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "Mesh.hpp"
#include "Validators.hpp"

namespace trinity::cad {

/// Validated parameter set for one part instance (mirrors PartIR.to_dict).
struct PartSpec {
    std::string type;
    std::string units = "mm";
    std::map<std::string, double> parameters;

    core::Json toJson() const;
};

/// Absolute bound for one parameter (mirrors ParameterInfo).
struct ParamBound {
    double min = 0.0;
    bool minExclusive = false;
    bool hasMax = false;
    double max = 0.0;
    bool integer = false;
    std::string unit = "mm";
};

struct PartDefinition {
    std::string name;
    std::string title;
    std::string description;
    std::map<std::string, double> defaults;
    std::map<std::string, ParamBound> bounds;
    std::vector<std::string> rules;

    std::function<PartSpec(const core::Json& raw)> fromRequest;
    std::function<Mesh(const PartSpec& spec)> builder;
    std::function<FrameValidation(const PartSpec& spec, const Mesh& mesh)> validator;
    std::function<std::string(const PartSpec& spec)> filenameStem;
};

/// All registered parts, sorted by name. Ported from parts.py; the
/// quadcopter_frame entry adapts the bespoke FrameParams path.
const std::vector<PartDefinition>& partRegistry();

/// Registry lookup. Returns nullptr for unknown types (the engine turns
/// that into the same "Unsupported CAD type" error as before).
const PartDefinition* findPart(const std::string& name);

/// Sorted names of every registered part (drives describe + tests).
std::vector<std::string> partNames();

}  // namespace trinity::cad
