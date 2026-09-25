#include "trinity/robotics/Validators.hpp"

#include <cmath>
#include <set>
#include <unordered_map>

namespace trinity::robotics {

namespace {

void addRule(RobotValidation& out, const std::string& rule, bool passed, std::string message) {
    out.rules.push_back(ValidationRule{rule, passed, std::move(message)});
    if (!passed) {
        out.passed = false;
    }
}

bool finiteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

}  // namespace

core::Json ValidationRule::toJson() const {
    return core::Json{{"rule", rule}, {"passed", passed}, {"message", message}};
}

core::Json RobotValidation::toJson() const {
    core::Json rulesJson = core::Json::array();
    for (const ValidationRule& rule : rules) {
        rulesJson.push_back(rule.toJson());
    }
    return core::Json{{"passed", passed}, {"rules", rulesJson}};
}

RobotValidation validateRobotProject(const RobotProject& project) {
    RobotValidation out;
    out.passed = true;
    const RobotModel& model = project.model;

    // --- base frame ---
    if (project.baseFrame.empty()) {
        addRule(out, "robot.base_frame", false, "Base frame name must not be empty");
    } else {
        addRule(out, "robot.base_frame", true, "Base frame '" + project.baseFrame + "'");
    }

    // --- links ---
    if (model.links.empty()) {
        addRule(out, "robot.links", false, "Robot model has no links");
    } else if (model.links.size() > static_cast<size_t>(kMaxLinks)) {
        addRule(out, "robot.links", false,
                "Robot model exceeds the maximum of " + std::to_string(kMaxLinks) + " links");
    } else {
        addRule(out, "robot.links", true,
                std::to_string(model.links.size()) + " link(s) declared");
    }
    {
        std::set<std::string> unique;
        bool duplicates = false;
        bool dimensions = true;
        std::string dimensionProblem;
        for (const Link& link : model.links) {
            if (link.name.empty()) {
                dimensions = false;
                dimensionProblem = "link with empty name";
            }
            if (!unique.insert(link.name).second) {
                duplicates = true;
            }
            if (!std::isfinite(link.lengthM)) {
                dimensions = false;
                dimensionProblem = "link '" + link.name + "' has non-finite length";
            } else if (link.lengthM < 0.0) {
                dimensions = false;
                dimensionProblem = "link '" + link.name + "' has negative length";
            } else if (link.lengthM > kMaxLinkLengthM) {
                dimensions = false;
                dimensionProblem = "link '" + link.name + "' length exceeds " +
                                   std::to_string(kMaxLinkLengthM) + " m";
            }
        }
        addRule(out, "robot.links.unique_names", !duplicates,
                duplicates ? "Duplicate link names" : "Link names are unique");
        addRule(out, "robot.links.dimensions", dimensions,
                dimensions ? "Link dimensions are finite and non-negative"
                           : "Invalid link dimension: " + dimensionProblem);
    }

    // --- joints ---
    if (model.joints.empty()) {
        addRule(out, "robot.joints", false, "Robot model has no joints");
    } else {
        addRule(out, "robot.joints", true,
                std::to_string(model.joints.size()) + " joint(s) declared");
    }
    {
        std::set<std::string> unique;
        bool duplicates = false;
        for (const Joint& joint : model.joints) {
            if (!unique.insert(joint.name).second) {
                duplicates = true;
            }
        }
        addRule(out, "robot.joints.unique_names", !duplicates,
                duplicates ? "Duplicate joint names" : "Joint names are unique");
    }

    // --- parent/child references ---
    {
        bool refsOk = true;
        std::string problem;
        for (const Joint& joint : model.joints) {
            if (joint.name.empty()) {
                refsOk = false;
                problem = "joint with empty name";
                break;
            }
            if (joint.parentLink.empty() || joint.childLink.empty()) {
                refsOk = false;
                problem = "joint '" + joint.name + "' has an empty parent or child link";
                break;
            }
            if (joint.parentLink == joint.childLink) {
                refsOk = false;
                problem = "joint '" + joint.name + "' connects a link to itself";
                break;
            }
            if (joint.parentLink != project.baseFrame && model.findLink(joint.parentLink) == nullptr) {
                refsOk = false;
                problem = "joint '" + joint.name + "' references unknown parent link '" +
                          joint.parentLink + "'";
                break;
            }
            if (model.findLink(joint.childLink) == nullptr) {
                refsOk = false;
                problem = "joint '" + joint.name + "' references unknown child link '" +
                          joint.childLink + "'";
                break;
            }
        }
        addRule(out, "robot.joints.parent_child", refsOk,
                refsOk ? "Joint parent/child references resolve"
                       : "Invalid parent/child reference: " + problem);
    }

    // --- axes and numeric values ---
    {
        bool axesOk = true;
        std::string axisProblem;
        bool numericOk = true;
        std::string numericProblem;
        for (const Joint& joint : model.joints) {
            if (!finiteVec(joint.originXYZ) || !finiteVec(joint.originRPYRad)) {
                numericOk = false;
                numericProblem = "joint '" + joint.name + "' origin has NaN/inf values";
            }
            if (!finiteVec(joint.axis)) {
                axesOk = false;
                axisProblem = "joint '" + joint.name + "' axis has NaN/inf values";
                continue;
            }
            if (joint.type == JointType::Revolute || joint.type == JointType::Prismatic) {
                const double norm = std::sqrt(joint.axis.x * joint.axis.x +
                                              joint.axis.y * joint.axis.y +
                                              joint.axis.z * joint.axis.z);
                if (norm <= 1e-12) {
                    axesOk = false;
                    axisProblem = "joint '" + joint.name + "' has a zero axis";
                }
            }
            if (joint.limit.specified) {
                if (!std::isfinite(joint.limit.lower) || !std::isfinite(joint.limit.upper) ||
                    !std::isfinite(joint.limit.velocityMax)) {
                    numericOk = false;
                    numericProblem = "joint '" + joint.name + "' limits contain NaN/inf";
                }
            }
        }
        addRule(out, "robot.joints.axis", axesOk,
                axesOk ? "Joint axes are finite and non-zero"
                       : "Invalid joint axis: " + axisProblem);
        addRule(out, "robot.joints.numeric", numericOk,
                numericOk ? "Joint numeric values are finite"
                          : "Non-finite joint value: " + numericProblem);
    }

    // --- limits ---
    {
        bool limitsOk = true;
        std::string problem;
        for (const Joint& joint : model.joints) {
            if (!joint.limit.specified) {
                continue;
            }
            if (joint.limit.lower > joint.limit.upper) {
                limitsOk = false;
                problem = "joint '" + joint.name + "' has lower > upper (" +
                          std::to_string(joint.limit.lower) + " > " +
                          std::to_string(joint.limit.upper) + ")";
                break;
            }
            if (joint.limit.velocityMax < 0.0) {
                limitsOk = false;
                problem = "joint '" + joint.name + "' has a negative velocity limit";
                break;
            }
        }
        addRule(out, "robot.joints.limits", limitsOk,
                limitsOk ? "Joint limits are consistent"
                         : "Invalid joint limits: " + problem);
    }

    // --- hierarchy acyclicity (all parents resolve without looping) ---
    {
        // Walk up parent links from every child; a repeated link means
        // a cycle. Parent refs are checked above; only resolve if they
        // were valid so this rule reports cycles specifically.
        std::unordered_map<std::string, std::string> parentOf;
        for (const Joint& joint : model.joints) {
            parentOf[joint.childLink] = joint.parentLink;
        }
        bool cycle = false;
        std::string cycleLink;
        for (const auto& [child, parent] : parentOf) {
            std::set<std::string> visited;
            std::string current = child;
            while (true) {
                if (!visited.insert(current).second) {
                    cycle = true;
                    cycleLink = current;
                    break;
                }
                const auto it = parentOf.find(current);
                if (it == parentOf.end()) {
                    break;  // reached the base frame or a root link
                }
                current = it->second;
                if (current == child) {
                    cycle = true;
                    cycleLink = child;
                    break;
                }
            }
            if (cycle) {
                break;
            }
        }
        addRule(out, "robot.hierarchy.acyclic", !cycle,
                cycle ? "Kinematic hierarchy contains a cycle through link '" + cycleLink + "'"
                      : "Kinematic hierarchy is acyclic");
    }

    // --- end effector ---
    {
        bool eeOk = true;
        std::string problem;
        if (project.endEffector.name.empty()) {
            eeOk = false;
            problem = "end-effector name must not be empty";
        } else if (!project.endEffector.parentLink.empty() &&
                   project.endEffector.parentLink != project.baseFrame &&
                   model.findLink(project.endEffector.parentLink) == nullptr) {
            eeOk = false;
            problem = "end-effector parent link '" + project.endEffector.parentLink +
                      "' does not exist";
        } else if (!finiteVec(project.endEffector.originXYZ)) {
            eeOk = false;
            problem = "end-effector origin has NaN/inf values";
        }
        addRule(out, "robot.end_effector", eeOk,
                eeOk ? "End effector '" + project.endEffector.name + "' is valid"
                     : "Invalid end effector: " + problem);
    }

    // --- actuated joint count ---
    {
        size_t actuated = 0;
        for (const Joint& joint : model.joints) {
            if (joint.type != JointType::Fixed) {
                ++actuated;
            }
        }
        const bool ok = actuated <= static_cast<size_t>(kMaxJoints);
        addRule(out, "robot.joints.actuated_count", ok,
                ok ? std::to_string(actuated) + " actuated joint(s) within limit"
                   : "Robot exceeds " + std::to_string(kMaxJoints) + " actuated joints");
    }

    return out;
}

RobotValidation validateJointState(const RobotProject& project, const JointState& state) {
    RobotValidation out;
    out.passed = true;

    bool namesOk = true;
    std::string nameProblem;
    bool valuesOk = true;
    std::string valueProblem;
    bool limitsOk = true;
    std::string limitProblem;

    const auto check = [&](const std::map<std::string, double>& values, const char* kind) {
        for (const auto& [name, value] : values) {
            const Joint* joint = project.model.findJoint(name);
            if (joint == nullptr) {
                namesOk = false;
                nameProblem = std::string(kind) + " references unknown joint '" + name + "'";
                continue;
            }
            if (!std::isfinite(value)) {
                valuesOk = false;
                valueProblem = std::string(kind) + " value for joint '" + name + "' is NaN/inf";
                continue;
            }
            if (std::fabs(value) > kMaxJointValue) {
                valuesOk = false;
                valueProblem = std::string(kind) + " value for joint '" + name +
                               "' exceeds the numeric bound ±" + std::to_string(kMaxJointValue);
                continue;
            }
            if (joint->limit.specified &&
                (value < joint->limit.lower - 1e-9 || value > joint->limit.upper + 1e-9)) {
                limitsOk = false;
                limitProblem = std::string(kind) + " for joint '" + name + "' (" +
                               std::to_string(value) + ") is outside limits [" +
                               std::to_string(joint->limit.lower) + ", " +
                               std::to_string(joint->limit.upper) + "]";
            }
        }
    };
    check(state.positions, "position");
    check(state.velocities, "velocity");

    addRule(out, "robot.state.names", namesOk,
            namesOk ? "Joint state references known joints"
                    : "Unknown joint in state: " + nameProblem);
    addRule(out, "robot.state.values", valuesOk,
            valuesOk ? "Joint state values are finite"
                     : "Invalid joint state value: " + valueProblem);
    addRule(out, "robot.state.limits", limitsOk,
            limitsOk ? "Joint state is within configured limits"
                     : "Joint state outside limits: " + limitProblem);
    return out;
}

}  // namespace trinity::robotics
