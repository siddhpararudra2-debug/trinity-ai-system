#pragma once

// Robotics model validation: structural checks rejected before any
// kinematics execution (duplicate names, dangling references, bad
// axes/limits/dimensions, NaN/inf, state outside limits, cycles).

#include <string>
#include <vector>

#include "Types.hpp"

namespace trinity::robotics {

struct ValidationRule {
    std::string rule;
    bool passed = false;
    std::string message;

    core::Json toJson() const;
};

struct RobotValidation {
    bool passed = false;
    std::vector<ValidationRule> rules;

    core::Json toJson() const;  // {passed, rules: [{rule, passed, message}]}
};

/// Full structural validation of a robot project (model + end
/// effector + base frame). Collects every rule — never fail-fast —
/// so callers can report all problems at once.
RobotValidation validateRobotProject(const RobotProject& project);

/// Joint-state checks against a project: unknown joint names, non-
/// finite values, positions outside configured limits.
RobotValidation validateJointState(const RobotProject& project, const JointState& state);

}  // namespace trinity::robotics
