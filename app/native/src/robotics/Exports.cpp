#include "trinity/robotics/Exports.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace trinity::robotics {
namespace {

std::string formatFixed(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

}  // namespace

void writeTrajectoryCsv(const std::string& path, const Trajectory& trajectory) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open CSV for writing: " + path);
    }
    file << "time";
    for (size_t k = 0; k < trajectory.jointOrder.size(); ++k) {
        file << ",joint_" << (k + 1) << "_position,joint_" << (k + 1) << "_velocity";
    }
    file << '\n';
    const size_t samples = trajectory.timestampsS.size();
    for (size_t i = 0; i < samples; ++i) {
        file << formatFixed(trajectory.timestampsS[i]);
        for (size_t k = 0; k < trajectory.jointOrder.size(); ++k) {
            const bool hasPos = k < trajectory.positions.size() && i < trajectory.positions[k].size();
            const bool hasVel = k < trajectory.velocities.size() && i < trajectory.velocities[k].size();
            file << ',' << formatFixed(hasPos ? trajectory.positions[k][i] : 0.0)
                 << ',' << formatFixed(hasVel ? trajectory.velocities[k][i] : 0.0);
        }
        file << '\n';
    }
    file.flush();
    if (!file) {
        throw std::runtime_error("Failed while writing CSV: " + path);
    }
}

void writeRobotJson(const std::string& path, const RobotProject& project,
                    const RobotResult* result, const core::Json& validation,
                    const core::Json& artifactReferences) {
    core::Json doc = core::Json::object();
    doc["project"] = project.toJson();
    doc["robot_name"] = project.robotName;
    doc["base_frame"] = project.baseFrame;
    doc["end_effector"] = project.endEffector.toJson();
    doc["initial_state"] = project.initialState.toJson();

    if (result != nullptr) {
        core::Json resultJson = result->toJson();
        // The full trajectory lives in the CSV artifact; keep a bounded
        // preview here (same policy as simulation exports).
        if (result->trajectory.sampleCount() > 0) {
            Trajectory full = result->trajectory;
            core::Json fullTrajectory = full.toJson();
            constexpr size_t kMaxPreview = 500;
            const size_t n = full.timestampsS.size();
            core::Json preview = core::Json::array();
            const size_t stride = n > kMaxPreview ? (n + kMaxPreview - 1) / kMaxPreview : 1;
            for (size_t i = 0; i < n; i += stride) {
                core::Json sample = core::Json::object();
                sample["t"] = full.timestampsS[i];
                core::Json pos = core::Json::array();
                core::Json vel = core::Json::array();
                for (size_t k = 0; k < full.positions.size(); ++k) {
                    pos.push_back(full.positions[k][i]);
                    vel.push_back(full.velocities[k][i]);
                }
                sample["positions"] = pos;
                sample["velocities"] = vel;
                preview.push_back(sample);
            }
            if (n > 0 && !preview.empty()) {
                core::Json last = core::Json::object();
                last["t"] = full.timestampsS.back();
                core::Json pos = core::Json::array();
                core::Json vel = core::Json::array();
                for (size_t k = 0; k < full.positions.size(); ++k) {
                    pos.push_back(full.positions[k].back());
                    vel.push_back(full.velocities[k].back());
                }
                last["positions"] = pos;
                last["velocities"] = vel;
                const double lastT = preview.back().value("t", -1.0);
                if (std::abs(lastT - full.timestampsS.back()) > 1e-12) {
                    preview.push_back(last);
                }
            }
            core::Json stripped = fullTrajectory;
            stripped.erase("samples");
            stripped["samples_in_csv_file"] = n;
            stripped["samples_embedded"] = 0;
            stripped["samples_note"] =
                "Full time series lives in the CSV artifact; see sample_preview.";
            stripped["sample_preview"] = preview;
            stripped["joint_names"] = core::Json(full.jointOrder);
            resultJson["trajectory"] = stripped;
            doc["trajectory_preview"] = preview;
            doc["trajectory"] = stripped;
        }
        doc["result"] = resultJson;
        doc["result_metadata"] = resultJson;
        doc["ik"] = resultJson.value("ik", core::Json::object());
        doc["frames"] = resultJson.value("frames", core::Json::object());
        doc["end_effector_pose"] = resultJson.value("end_effector", core::Json::object());
    }
    doc["validation"] = validation;
    doc["artifacts"] = artifactReferences;
    doc["artifact_references"] = artifactReferences;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open JSON for writing: " + path);
    }
    file << doc.dump(2);
    file.flush();
    if (!file) {
        throw std::runtime_error("Failed while writing JSON: " + path);
    }
}

}  // namespace trinity::robotics
