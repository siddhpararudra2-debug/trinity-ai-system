#include "CadEngine.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "../../core/FileSystem.hpp"
#include "../../core/Paths.hpp"
#include "../../core/Uuid.hpp"

namespace trinity::engines::cad {
namespace {

std::pair<Vec3, Vec3> bbox(const Mesh& mesh) { return mesh.bounding_box(); }

}  // namespace

// ------------------------------------------------------------ MeshKernelAdapter

ICADAdapter::GenerateResult MeshKernelAdapter::generate(const QuadcopterFrameIR& ir) {
    GenerateResult result;
    result.mesh = build_quadcopter_frame(ir);
    result.ok = true;
    return result;
}

bool MeshKernelAdapter::import_model(const std::string& path, Mesh& out_mesh) {
    // Mesh-kernel import: STL binary only (parse triangle records).
    auto contents = core::FileSystem::read_file(path);
    if (!contents.has_value() || contents->size() < 84) return false;
    const std::string& data = *contents;
    if (data.substr(0, 5) == "solid" && data.find("facet") != std::string::npos) {
        return false;  // ASCII STL unsupported by the fast path; honest refusal
    }
    std::uint32_t count = 0;
    std::memcpy(&count, data.data() + 80, 4);
    const std::size_t expected =
        84 + static_cast<std::size_t>(count) * 50;
    if (data.size() < expected) return false;

    out_mesh.triangles.clear();
    std::size_t offset = 84;
    auto read_f32 = [&](std::size_t at) {
        float f = 0.0f;
        std::uint32_t bits = 0;
        std::memcpy(&bits, data.data() + at, 4);
        std::memcpy(&f, &bits, 4);
        return static_cast<double>(f);
    };
    for (std::uint32_t i = 0; i < count; ++i) {
        Vec3 v[3];
        for (int vi = 0; vi < 3; ++vi) {
            const std::size_t base = offset + 12 + static_cast<std::size_t>(vi) * 12;
            v[vi] = {read_f32(base), read_f32(base + 4), read_f32(base + 8)};
        }
        out_mesh.triangles.emplace_back(v[0], v[1], v[2]);
        offset += 50;
    }
    return true;
}

bool MeshKernelAdapter::export_model(const Mesh& mesh, const std::string& format,
                                     const std::string& output_path) {
    if (format != "stl") return false;  // STEP/3MF need a real kernel: honest refusal
    std::error_code ec;
    return write_binary_stl(mesh, output_path, ec);
}

core::Json MeshKernelAdapter::validate(const QuadcopterFrameIR& ir, const Mesh& mesh) {
    return validate_quadcopter_frame(ir, mesh).checks;
}

core::Json MeshKernelAdapter::preview(const Mesh& mesh) {
    const auto [min_c, max_c] = bbox(mesh);
    core::Json out = core::Json::object();
    out["triangle_count"] = static_cast<double>(mesh.triangle_count());
    core::Json min_json = core::Json::array();
    min_json.push_back(std::get<0>(min_c));
    min_json.push_back(std::get<1>(min_c));
    min_json.push_back(std::get<2>(min_c));
    core::Json max_json = core::Json::array();
    max_json.push_back(std::get<0>(max_c));
    max_json.push_back(std::get<1>(max_c));
    max_json.push_back(std::get<2>(max_c));
    core::Json box = core::Json::object();
    box["min"] = min_json;
    box["max"] = max_json;
    out["bounding_box_mm"] = box;
    return out;
}

// ------------------------------------------------------- UnavailableKernelAdapter

ICADAdapter::GenerateResult UnavailableKernelAdapter::generate(const QuadcopterFrameIR& ir) {
    (void)ir;
    GenerateResult result;
    result.ok = false;
    result.error = kernel_hint_ + " kernel is not installed in this build";
    return result;
}

bool UnavailableKernelAdapter::import_model(const std::string& path, Mesh&) {
    (void)path;
    return false;
}

bool UnavailableKernelAdapter::export_model(const Mesh&, const std::string&,
                                            const std::string&) {
    return false;
}

core::Json UnavailableKernelAdapter::validate(const QuadcopterFrameIR&, const Mesh&) {
    return core::Json::object();
}

core::Json UnavailableKernelAdapter::preview(const Mesh&) { return core::Json::object(); }

// -------------------------------------------------------------------- engine

CadEngine::CadEngine() {
    adapters_.push_back(std::make_shared<MeshKernelAdapter>());
    adapters_.push_back(std::make_shared<UnavailableKernelAdapter>(
        "cadquery", "CadQuery/OpenCascade"));
    adapters_.push_back(std::make_shared<UnavailableKernelAdapter>("freecad", "FreeCAD"));
    adapters_.push_back(std::make_shared<UnavailableKernelAdapter>("openscad", "OpenSCAD"));
    adapters_.push_back(std::make_shared<UnavailableKernelAdapter>("onshape", "Onshape"));
    active_adapter_ = adapters_.front();
}

void CadEngine::register_adapter(std::shared_ptr<ICADAdapter> adapter) {
    adapters_.push_back(std::move(adapter));
    active_adapter_ = adapters_.back();
}

std::vector<std::string> CadEngine::adapter_names() const {
    std::vector<std::string> names;
    names.reserve(adapters_.size());
    for (const auto& adapter : adapters_) names.push_back(adapter->name());
    return names;
}

EngineDescriptor CadEngine::describe() const {
    EngineDescriptor descriptor;
    descriptor.id = "cad";
    descriptor.name = "CAD Engine";
    descriptor.version = "1.0";
    descriptor.capabilities = {"generate", "validate", "export", "preview", "import"};
    descriptor.input_schema.fields = {
        {"type", "string (quadcopter_frame)"},
        {"parameters", "object: overall_size, arm_width, plate_thickness, "
                       "motor_mount_diameter, fc_mount_spacing, center_plate_size"},
        {"outputs", "array: stl | json (glb/step via engine host when available)"}};
    descriptor.output_schema.fields = {
        {"spec", "IR object"}, {"triangle_count", "number"},
        {"bounding_box_mm", "object"}, {"artifacts", "array of artifact refs"}};
    descriptor.health = EngineHealth::Healthy;
    descriptor.health_detail = "mesh kernel; external kernels report unavailable honestly";
    return descriptor;
}

ExecutionOutput CadEngine::execute(const std::string& capability, const core::Json& parameters) {
    if (capability != "generate") {
        throw core::TrinityException(core::Error(
            core::ErrorCode::RequestValidationError,
            "CAD engine has no operation '" + capability + "'"));
    }

    const std::string part_type =
        parameters.contains("type") ? parameters.find("type")->as_string() : "quadcopter_frame";
    if (part_type != "quadcopter_frame") {
        core::Json details = core::Json::object();
        core::Json supported = core::Json::array();
        supported.push_back(core::Json("quadcopter_frame"));
        details["supported"] = supported;
        throw core::TrinityException(core::Error(
            core::ErrorCode::RequestValidationError,
            "Unsupported CAD type '" + part_type + "'", details));
    }

    const QuadcopterFrameIR ir =
        QuadcopterFrameIR::from_request(parameters.contains("parameters")
                                            ? *parameters.find("parameters")
                                            : core::Json::object());

    ICADAdapter::GenerateResult generated = active_adapter_->generate(ir);
    if (!generated.ok) {
        throw core::TrinityException(core::Error(
            core::ErrorCode::CapabilityUnavailableError, generated.error,
            [&] {
                core::Json details = core::Json::object();
                details["code"] = "CAD_KERNEL_UNAVAILABLE";
                details["adapter"] = active_adapter_->name();
                return details;
            }()));
    }

    const ValidationReport report = validate_quadcopter_frame(ir, generated.mesh);
    if (!report.passed) {
        throw core::TrinityException(core::Error(
            core::ErrorCode::GeometryValidationError,
            "Generated geometry failed validation",
            [&] {
                core::Json details = core::Json::object();
                details["checks"] = report.checks;
                return details;
            }()));
    }

    ExecutionOutput output;
    output.success = true;
    output.result = ir.to_json();
    output.result["triangle_count"] = static_cast<double>(generated.mesh.triangle_count());
    output.result["preview"] = active_adapter_->preview(generated.mesh);
    output.validation_status = "VALIDATED";
    output.validation_checks = report.checks;

    // Export to scratch; the job caller moves these into the ArtifactStore.
    std::vector<std::string> outputs;
    if (parameters.contains("outputs") && parameters.find("outputs")->is_array()) {
        for (const core::Json& item : parameters.find("outputs")->as_array()) {
            outputs.push_back(item.as_string());
        }
    }
    if (outputs.empty()) outputs = {"stl", "json"};

    const std::string scratch =
        core::FileSystem::make_temp_directory(core::FileSystem::normalise(
                                                  core::Paths::temp_dir()).generic_string(),
                                              "cad_");
    if (!scratch.empty()) {
        const std::string base = part_type + "_" +
                                 std::to_string(static_cast<long long>(
                                     ir.parameters().at("overall_size"))) + "mm_" +
                                 core::new_uuid().substr(0, 8);
        for (const std::string& format : outputs) {
            if (format == "stl") {
                const std::string path = scratch + "/" + base + ".stl";
                std::error_code ec;
                if (write_binary_stl(generated.mesh, path, ec)) {
                    output.pending_artifacts.emplace_back(path, "stl");
                }
            } else if (format == "json") {
                const std::string path = scratch + "/" + base + ".json";
                std::error_code json_ec;
                core::FileSystem::write_file_atomic(path, ir.to_json().dump(2), json_ec);
                output.pending_artifacts.emplace_back(path, "json");
            } else if (format == "glb" || format == "step" || format == "3mf") {
                output.result["unavailable_formats"][format] =
                    "CAD_KERNEL_UNAVAILABLE: " + format +
                    " requires an external CAD kernel (export via the Python engine host for GLB).";
            }
        }
    }
    return output;
}

core::Json CadEngine::validate(const core::Json& payload) {
    // Re-validate a stored spec by regenerating deterministically.
    const QuadcopterFrameIR ir = QuadcopterFrameIR::from_request(payload);
    const Mesh mesh = build_quadcopter_frame(ir);
    const ValidationReport report = validate_quadcopter_frame(ir, mesh);
    core::Json out = core::Json::object();
    out["passed"] = report.passed;
    out["checks"] = report.checks;
    return out;
}

EngineHealth CadEngine::health() const { return EngineHealth::Healthy; }

void register_cad_engine(std::vector<std::string>& registered) {
    static std::shared_ptr<CadEngine> engine = std::make_shared<CadEngine>();
    if (EngineRegistry::instance().register_engine(engine).is_ok()) {
        registered.push_back("cad");
    }
}

}  // namespace trinity::engines::cad
