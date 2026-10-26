#include "trinity/engines/CadEngine.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

#include "trinity/cad/Csg.hpp"
#include "trinity/cad/GlbWriter.hpp"
#include "trinity/cad/Parts.hpp"
#include "trinity/cad/StepWriter.hpp"
#include "trinity/cad/StlWriter.hpp"
#include "trinity/cad/Validators.hpp"
#include "trinity/core/Logger.hpp"
#include "trinity/core/Uuid.hpp"

namespace trinity::engines {
namespace fs = std::filesystem;

namespace {

std::string shortId() {
    std::string uuid = core::newUuid();
    uuid.erase(std::remove(uuid.begin(), uuid.end(), '-'), uuid.end());
    return uuid.substr(0, 8);
}

core::Json boundingBoxJson(const cad::BoundingBox& bbox) {
    return core::Json{{bbox.min[0], bbox.min[1], bbox.min[2]},
                      {bbox.max[0], bbox.max[1], bbox.max[2]}};
}

// Writes every requested mesh artifact (stl / glb / step) under `stem` and
// appends them as pending artifacts. "json" payloads are op-specific and
// stay with their own execution path. Throws EngineExecutionError mapping
// writer failures to the engine's structured surface.
void emitMeshArtifacts(const cad::Mesh& mesh, const std::string& stem,
                       const std::vector<std::string>& outputs,
                       EngineResult& out) {
    const auto wants = [&outputs](const char* type) {
        return std::find(outputs.begin(), outputs.end(), type) !=
               outputs.end();
    };
    if (wants("stl")) {
        try {
            cad::writeBinaryStl(mesh, stem + ".stl");
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(
                "Cannot write STL artifact: " + std::string(exc.what()), {},
                "engines");
        }
        out.pendingArtifacts.emplace_back(stem + ".stl", "stl");
    }
    if (wants("glb")) {
        try {
            cad::writeGlb(mesh, stem + ".glb");
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(
                "Cannot write GLB artifact: " + std::string(exc.what()), {},
                "engines");
        }
        out.pendingArtifacts.emplace_back(stem + ".glb", "glb");
    }
    if (wants("step")) {
        try {
            cad::writeStep(mesh, stem + ".step", stem);
        } catch (const std::exception& exc) {
            throw core::EngineExecutionError(
                "Cannot write STEP artifact: " + std::string(exc.what()), {},
                "engines");
        }
        out.pendingArtifacts.emplace_back(stem + ".step", "step");
    }
}

// Builds one boolean operand mesh from a {"type": ..., ...parameters}
// object. The operand's own part validators must pass: broken operands
// refuse before any boolean work happens.
cad::Mesh buildOperandMesh(const core::Json& operand, const std::string& side) {
    if (!operand.is_object()) {
        throw core::RequestValidationError(
            "Boolean operand '" + side +
                "' must be an object with {'type': ..., <parameters>}",
            {{"side", side}}, "engines");
    }
    const std::string type = operand.value("type", "");
    const cad::PartDefinition* part = cad::findPart(type);
    if (part == nullptr) {
        core::Json supported = core::Json::array();
        for (const auto& name : cad::partNames()) {
            supported.push_back(name);
        }
        throw core::RequestValidationError(
            "Boolean operand '" + side + "' has unsupported type '" + type +
                "'",
            {{"side", side}, {"supported", supported}}, "engines");
    }
    core::Json parameters = core::Json::object();
    for (auto it = operand.begin(); it != operand.end(); ++it) {
        if (it.key() != "type") {
            parameters[it.key()] = it.value();
        }
    }
    const cad::PartSpec spec = part->fromRequest(parameters);
    const cad::Mesh mesh = part->builder(spec);
    const cad::FrameValidation validation = part->validator(spec, mesh);
    if (!validation.ok) {
        throw core::GeometryValidationError(
            "Boolean operand '" + side + "' (" + type +
                ") failed part validation",
            {{"side", side}, {"checks", validation.checks}}, "engines");
    }
    // Builders glue overlapping closed shells into one soup; boolean
    // operands must be single solids for BSP clipping, so fold the shells
    // into one watertight mesh (single-shell parts pass through).
    return cad::unionMeshComponents(mesh);
}

}  // namespace

CadEngine::CadEngine() {
    name_ = "cad";
    version_ = "0.4.0";
    capabilities_ = {"generate", "describe", "boolean"};
}

EngineResult CadEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        core::Json supported = core::Json::array();
        for (const auto& type : cad::partNames()) {
            supported.push_back(type);
        }
        EngineResult out = successResult(
            request, {{"engine", name_},
                      {"version", version_},
                      {"capabilities", capabilities_},
                      {"supported_types", supported}});
        out.validation = validate(out);
        return out;
    }
    if (request.operation == "generate") {
        return executeGenerate(request);
    }
    if (request.operation == "boolean") {
        return executeBoolean(request);
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
    const cad::PartDefinition* part = cad::findPart(partType);
    if (part == nullptr) {
        core::Json supported = core::Json::array();
        for (const auto& type : cad::partNames()) {
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
    const cad::PartSpec spec =
        part->fromRequest(request.parameters.value("parameters", core::Json(nullptr)));
    const cad::Mesh mesh = part->builder(spec);
    const cad::FrameValidation validation = part->validator(spec, mesh);
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
    const std::string baseName = part->filenameStem(spec) + "_" + shortId();

    EngineResult out = successResult(request, core::Json::object());
    const std::string stem = (workDir / baseName).string();
    emitMeshArtifacts(mesh, stem, outputs, out);
    const bool wantJson = std::find(outputs.begin(), outputs.end(), "json") !=
                          outputs.end();
    if (wantJson) {
        const std::string jsonPath = stem + ".json";
        std::ofstream jsonFile(jsonPath, std::ios::binary | std::ios::trunc);
        if (!jsonFile) {
            throw core::EngineExecutionError("Cannot write JSON artifact", {},
                                             "engines");
        }
        jsonFile << spec.toJson().dump(2);
        jsonFile.close();
        if (!jsonFile) {
            throw core::EngineExecutionError("Failed while writing JSON artifact", {},
                                             "engines");
        }
        out.pendingArtifacts.emplace_back(jsonPath, "json");
    }

    const cad::BoundingBox bbox = mesh.boundingBox();
    core::Json result{{"type", partType},
                      {"spec", spec.toJson()},
                      {"triangle_count", static_cast<int>(mesh.triangleCount())},
                      {"bounding_box_mm", boundingBoxJson(bbox)}};
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

EngineResult CadEngine::executeBoolean(const EngineRequest& request) {
    std::string mode = request.parameters.value("mode", "subtract");
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    cad::CsgOperation op = cad::CsgOperation::Subtract;
    if (mode == "union") {
        op = cad::CsgOperation::Union;
    } else if (mode == "intersect") {
        op = cad::CsgOperation::Intersect;
    } else if (mode == "subtract") {
        op = cad::CsgOperation::Subtract;
    } else {
        throw core::RequestValidationError(
            "Unknown boolean mode '" + mode + "' (union, intersect, subtract)",
            {{"mode", mode}}, "engines");
    }

    core::Json outputsJson =
        request.parameters.value("outputs", core::Json::array());
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

    const cad::Mesh meshA = buildOperandMesh(
        request.parameters.contains("a") ? request.parameters["a"]
                                         : core::Json(nullptr),
        "a");
    const cad::Mesh meshB = buildOperandMesh(
        request.parameters.contains("b") ? request.parameters["b"]
                                         : core::Json(nullptr),
        "b");
    const cad::Mesh resultMesh = cad::booleanMesh(meshA, meshB, op);

    const double volumeA = cad::signedVolume(meshA);
    const double volumeB = cad::signedVolume(meshB);
    const double volumeR = cad::signedVolume(resultMesh);
    core::Json checks = core::Json::object();
    checks["operands_valid"] = true;
    checks["result_finite"] = std::isfinite(volumeR);

    // Containment sanity from the closed-set definition of each op —
    // violated bounds mean the CSG pass lied somewhere and must fail loud.
    // Only bounds that hold even for self-intersecting operand surfaces
    // (part builders emit overlapping primitives) are enforced:
    // subtract/intersect cannot exceed the larger/smaller operand's
    // enclosed volume, union cannot exceed a + b.
    const double tolerance =
        1e-6 * (1.0 + std::max(std::fabs(volumeA), std::fabs(volumeB)));
    bool boundsHold = true;
    if (op == cad::CsgOperation::Subtract && volumeR > volumeA + tolerance) {
        boundsHold = false;
    }
    if (op == cad::CsgOperation::Intersect &&
        volumeR > std::min(volumeA, volumeB) + tolerance) {
        boundsHold = false;
    }
    if (op == cad::CsgOperation::Union &&
        volumeR > volumeA + volumeB + tolerance) {
        boundsHold = false;
    }
    checks["volume_within_operand_bounds"] = boundsHold;

    // Empty results are honest defined outcomes (disjoint intersect etc.);
    // empty solids are reported with no artifacts, never faked as files.
    const bool empty = resultMesh.empty();
    if (!empty) {
        const bool watertight = cad::isWatertight(resultMesh);
        checks["result_watertight"] = watertight;
        if (!watertight || !boundsHold) {
            throw core::EngineExecutionError(
                !watertight
                    ? "Boolean result is not watertight"
                    : "Boolean result volume violates operand bounds",
                {{"mode", mode}, {"checks", checks}}, "engines");
        }
    } else {
        checks["result_watertight"] = true;  // vacuous for empty
        if (!boundsHold) {
            throw core::EngineExecutionError(
                "Boolean result volume violates operand bounds",
                {{"mode", mode}, {"checks", checks}}, "engines");
        }
    }

    const std::string aType = request.parameters["a"].value("type", "");
    const std::string bType = request.parameters["b"].value("type", "");

    core::Json data{{"type", "boolean"},
                    {"mode", mode},
                    {"a_type", aType},
                    {"b_type", bType},
                    {"outcome", empty ? "empty" : "solid"},
                    {"triangle_count",
                     static_cast<int>(resultMesh.triangleCount())},
                    {"volumes",
                     core::Json{{"a", volumeA}, {"b", volumeB},
                                {"result", volumeR}}}};
    EngineResult out = successResult(request, data);
    out.metadata["cad_checks"] = checks;

    if (!empty) {
        const std::string stem =
            (fs::temp_directory_path() /
             ("trinity_cad_boolean_" + shortId()))
                .string();
        emitMeshArtifacts(resultMesh, stem, outputs, out);
        const bool wantJson =
            std::find(outputs.begin(), outputs.end(), "json") !=
            outputs.end();
        if (wantJson) {
            // Boolean summary artifact in place of generate's spec json:
            // inputs plus volumes, so the persisted artifact is auditable.
            std::ofstream file(stem + ".json",
                               std::ios::binary | std::ios::trunc);
            if (!file) {
                throw core::EngineExecutionError(
                    "Cannot write JSON artifact", {}, "engines");
            }
            file << data.dump(2);
            file.close();
            if (!file) {
                throw core::EngineExecutionError(
                    "Failed while writing JSON artifact", {}, "engines");
            }
            out.pendingArtifacts.emplace_back(stem + ".json", "json");
        }
        data["bounding_box_mm"] = boundingBoxJson(resultMesh.boundingBox());
        out.result = data;
    }
    out.validation = validate(out);
    core::Logger::instance().info(
        "engines", "cad boolean",
        core::Json{{"mode", mode},
                   {"triangles", static_cast<int>(resultMesh.triangleCount())},
                   {"outcome", empty ? "empty" : "solid"}});
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
    if (result.operation == "boolean") {
        validation.checks =
            result.metadata.value("cad_checks", validation.checks);
        const std::string outcome =
            result.result.value("outcome", "solid");
        if (outcome == "empty") {
            validation.status = validation::ValidationStatus::Validated;
            validation.message = "Boolean produced an empty solid honestly";
            validation::ValidationMessage emptyMsg;
            emptyMsg.rule = "cad.boolean";
            emptyMsg.severity = validation::Severity::Info;
            emptyMsg.passed = true;
            emptyMsg.message =
                "empty result (disjoint operands), no artifacts claimed";
            validation.addMessage(std::move(emptyMsg));
            return validation;
        }
        validation.status = validation::ValidationStatus::Validated;
        validation.message = "Boolean geometry passed watertight and volume checks";
        validation::ValidationMessage msg;
        msg.rule = "cad.geometry_valid";
        msg.severity = validation::Severity::Info;
        msg.passed = true;
        msg.message = "watertight, finite, within operand volume bounds";
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
