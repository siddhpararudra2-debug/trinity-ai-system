#include "trinity/simulation/Exports.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace trinity::simulation {
namespace {

std::string formatFixed(double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

}  // namespace

void writeCsv(const std::string& path, const SimulationResult& result) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open CSV for writing: " + path);
    }
    file << "time_s,position_x,position_y,position_z,velocity_x,velocity_y,velocity_z,"
            "acceleration_x,acceleration_y,acceleration_z\n";
    for (const auto& sample : result.samples) {
        file << formatFixed(sample.t) << ',' << formatFixed(sample.position.x) << ','
             << formatFixed(sample.position.y) << ',' << formatFixed(sample.position.z) << ','
             << formatFixed(sample.velocity.x) << ',' << formatFixed(sample.velocity.y) << ','
             << formatFixed(sample.velocity.z) << ',' << formatFixed(sample.acceleration.x) << ','
             << formatFixed(sample.acceleration.y) << ',' << formatFixed(sample.acceleration.z)
             << '\n';
    }
    file.flush();
    if (!file) {
        throw std::runtime_error("Failed while writing CSV: " + path);
    }
}

void writeJson(const std::string& path, const SimulationProject& project,
               const SimulationResult& result, const core::Json& validation) {
    core::Json doc = core::Json::object();
    doc["project"] = project.toJson();
    doc["result"] = result.toJson();
    doc["validation"] = validation;
    doc["artifacts"] = core::Json::array();
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

}  // namespace trinity::simulation
