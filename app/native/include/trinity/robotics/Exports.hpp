#pragma once

#include <string>

#include "Types.hpp"

namespace trinity::robotics {

/// CSV for a joint trajectory. Header (spec form):
///   time,joint_1_position,joint_1_velocity,joint_2_position,...
/// Column order follows Trajectory::jointOrder (the joint name map
/// lives in the JSON export).
void writeTrajectoryCsv(const std::string& path, const Trajectory& trajectory);

/// Structured JSON export: robot project (model, limits, state),
/// result metadata (FK frames / IK status / validation), bounded
/// trajectory preview, validation state, and artifact references.
/// The complete time series stays in the CSV artifact.
void writeRobotJson(const std::string& path, const RobotProject& project,
                    const RobotResult* result, const core::Json& validation,
                    const core::Json& artifactReferences = core::Json::array());

}  // namespace trinity::robotics
