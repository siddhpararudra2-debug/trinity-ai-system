#include "trinity/engines/CadEngine.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/StlWriter.hpp"
#include "trinity/cad/Validators.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

const std::vector<std::string> kSupportedTypes = {"quadcopter_frame"};
const std::vector<std::string> kStepFormats = {"step"};

std::string shortId() {
    std::string uuid = core::newUuid();
    uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    return uuid.substr(0, 8);
}

core::Json boundingBoxJson(const cad::BoundingBox& bbox) {
    return core::Json{{bbox.min[0], bbox.min[1], bbox.min[2]},
                      {bbox.max[0], bbox.max[1], bbox.max[2]}};
}

}  // namespace

CadEngine::CadEngine() {
    name_ = "cad";
    version_ = "0.2.0";
    capabilities_ = {"generate", "describe"};
}

EngineResult CadEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out = successResult(
            request, {{"engine", name_},
                      {"version", version_},
                      {"capabilities", capabilities_},
                      {"supported_types", kSupportedTypes}});
        out.validation = validate(out);
        return out;
    }
    if (request.operation == "generate") {
        return executeGenerate(request);
    }
    // Unknown operations refuse truthfully — never fake geometry.
    EngineResult out = capabilityUnavailable(
        request, "CAD operation '" + request.operation + "' is not implemented yet");
    core::Logger::instance().warning(
        "engines", "cad unsupported operation",
        core::Json{{"operation", request.operation}});
    return out;
}

EngineResult CadEngine::executeGenerate(const EngineRequest& request) {
    const std::string partType = request.parameters.value("type", "quadcopter_frame");
    if (std::find(kSupportedTypes.begin(), kSupportedTypes.end(), partType) ==
        kSupportedTypes.end()) {
        core::Json supported = core::Json::array();
        for (const auto& type : kSupportedTypes) {
            supported.push_back(type);
        }
        throw core::RequestValidationError("Unsupported CAD type '" + partType + "'",
                                           {{"supported", supported}}, "engines");
    }

    core::Json outputsJson = request.parameters.value("outputs", core::Json::array());
    std::vector<std::string> outputs;
    if (outputsJson.is_array()) {
        for (const auto& item : outputsJson) {
            if (item.is_string()) {
                outputs.push_back(item.get<std::string>());
            }
        }
    }
    if (outputs.empty()) {
        outputs = {"stl", "json"};
    }

    // May throw RequestValidationError for bad parameters.
    const cad::FrameParams params = cad::FrameParams::fromRequest(
        request.parameters.value("parameters", core::Json(nullptr)));
    const cad::Mesh mesh = cad::buildQuadcopterFrame(params);
    const cad::FrameValidation validation = cad::validateQuadcopterFrame(params, mesh);
    if (!validation.ok) {
        throw core::GeometryValidationError("Generated geometry failed validation",
                                            {{"checks", validation.checks}}, "engines");
    }

    // Scratch files; JobManager promotes them into managed storage.
    std::error_code ec;
    const fs::path workDir = fs::temp_directory_path(ec) / ("trinity_cad_" + shortId());
    fs::create_directories(workDir, ec);
    if (ec) {
        throw core::EngineExecutionError(
            "Cannot create CAD scratch directory: " + ec.message(), {}, "engines");
    }
    const int overallMm = static_cast<int>(params.parameters.at("overall_size"));
    const std::string baseName =
        partType + "_" + std::to_string(overallMm) + "mm_" + shortId();

    EngineResult out = successResult(request, core::Json::object());
    const bool wantStl = std::find(outputs.begin(), outputs.end(), "stl") != outputs.end();
    const bool wantJson = std::find(outputs.begin(), outputs.end(), "json") != outputs.end();
    if (wantStl) {
        const std::string stlPath = (workDir / (baseName + ".stl")).string();
        try {
            cad::writeBinaryStl(mesh, stlPath);
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(
                std::string("Cannot write STL artifact: ") + exc.what(), {}, "engines");
        }
        out.pendingArtifacts.emplace_back(stlPath, "stl");
    }
    if (wantJson) {
        const std::string jsonPath = (workDir / (baseName + ".json")).string();
        std::ofstream jsonFile(jsonPath, std::ios::binary | std::ios::trunc);
        if (!jsonFile) {
            throw core::EngineExecutionError("Cannot write JSON artifact", {}, "engines");
        }
        jsonFile << params.toJson().dump(2);
        jsonFile.close();
        if (!jsonFile) {
            throw core::EngineExecutionError("Failed while writing JSON artifact", {},
                                             "engines");
        }
        out.pendingArtifacts.emplace_back(jsonPath, "json");
    }

    const cad::BoundingBox bbox = mesh.boundingBox();
    core::Json result{{"type", partType},
                      {"spec", params.toJson()},
                      {"triangle_count", static_cast<int>(mesh.triangleCount())},
                      {"bounding_box_mm", boundingBoxJson(bbox)}};
    core::Json unavailable = core::Json::object();
    for (const auto& fmt : outputs) {
        if (std::find(kStepFormats.begin(), kStepFormats.end(), fmt) != kStepFormats.end()) {
            unavailable[fmt] =
                "CAD_KERNEL_UNAVAILABLE: STEP requires CadQuery/OpenCascade or another "
                "real CAD kernel.";
        }
    }
    if (!unavailable.empty()) {
        result["unavailable_formats"] = unavailable;
    }
    out.result = std::move(result);
    out.metadata["cad_checks"] = validation.checks;
    out.validation = validate(out);

    core::Logger::instance().info(
        "engines", "cad generate",
        core::Json{{"type", partType},
                   {"triangles", static_cast<int>(mesh.triangleCount())},
                   {"artifacts", static_cast<int>(out.pendingArtifacts.size())}});
    return out;
}

validation::ValidationResult CadEngine::validate(const EngineResult& result) const {
    validation::ValidationResult validation;
    validation.operation = result.operation;
    validation.jobId = result.jobId;
    validation.checks = {{"engine", "cad"}, {"operation", result.operation}};
    if (!result.success) {
        validation.status = validation::ValidationStatus::Invalid;
        validation.message = "CAD operation unavailable or failed";
        validation::ValidationMessage msg;
        msg.rule = "cad.available";
        msg.severity = validation::Severity::Warning;
        msg.passed = false;
        msg.message = "No CAD geometry produced";
        validation.addMessage(std::move(msg));
        if (!result.errors.empty()) {
            validation.error = result.errors.front();
        }
        return validation;
    }
    if (result.operation == "generate") {
        validation.checks = result.metadata.value("cad_checks", validation.checks);
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Generated geometry passed all checks";
        validation::ValidationMessage msg;
        msg.rule = "cad.geometry_valid";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = "dimensions, topology, clearances, manufacturability";
        validation.addMessage(std::move(msg));
        return validation;
    }
    validation.status = validation::ValidationStatus::Generated;
    validation.message = "CAD describe completed";
    validation::ValidationMessage msg;
    msg.rule = "cad.describe";
    msg.severity = validation::Severity::Info;
    msg.passed = true;
    msg.message = "Capability metadata returned, no geometry claimed";
    validation.addMessage(std::move(msg));
    return validation;
}

}  // namespace trinity::engines
