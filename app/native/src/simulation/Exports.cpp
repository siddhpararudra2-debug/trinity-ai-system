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
               const SimulationResult& result, const core::Json& validation,
               const core::Json& artifactReferences) {
    // Result metadata without the full sample series: the CSV artifact
    // carries the complete time series, so duplicating it here would
    // only bloat the export (§7/§10: no unnecessary duplication).
    core::Json resultMetadata = result.toJson();
    resultMetadata.erase("samples");
    resultMetadata["samples_in_samples_file"] = result.sampleCount;
    resultMetadata["samples_embedded"] = 0;
    resultMetadata["samples_note"] =
        "Full time series lives in the CSV artifact; see sample_preview.";

    // Bounded, evenly strided preview so the JSON stays self-describing.
    constexpr size_t kMaxPreview = 500;
    core::Json preview = core::Json::array();
    if (!result.samples.empty()) {
        const size_t n = result.samples.size();
        const size_t stride = n > kMaxPreview ? (n + kMaxPreview - 1) / kMaxPreview : 1;
        for (size_t i = 0; i < n; i += stride) {
            preview.push_back(result.samples[i].toJson());
        }
        if (preview.back() != result.samples.back().toJson()) {
            preview.push_back(result.samples.back().toJson());
        }
    }

    core::Json doc = core::Json::object();
    const core::Json projectJson = project.toJson();
    doc["project"] = projectJson;
    doc["parameters"] = projectJson.value("parameters", core::Json::array());
    doc["initial_conditions"] = project.initial.toJson();
    doc["integration_method"] = result.method;
    doc["time_step_s"] = result.dt;
    doc["duration_s"] = result.durationS;
    doc["result"] = resultMetadata;
    doc["result_metadata"] = resultMetadata;
    doc["sample_preview"] = preview;
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

}  // namespace trinity::simulation
