#include "QuadcopterFrame.hpp"

#include <cmath>

#include "../../core/Error.hpp"

namespace trinity::engines::cad {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMinPrintableFeatureMm = 1.0;  // conservative FDM floor

[[noreturn]] void reject(const std::string& message, core::Json details = core::Json::object()) {
    throw core::TrinityException(
        core::Error(core::ErrorCode::RequestValidationError, message, std::move(details)));
}

const std::map<std::string, double>& defaults() {
    static const std::map<std::string, double> kDefaults = {
        {"overall_size", 50.0},          {"motor_count", 4.0},
        {"arm_width", 5.0},              {"plate_thickness", 1.5},
        {"motor_mount_diameter", 6.0},   {"fc_mount_spacing", 25.0},
        {"center_plate_size", 26.0},
    };
    return kDefaults;
}

}  // namespace

QuadcopterFrameIR QuadcopterFrameIR::from_request(const core::Json& raw_parameters) {
    QuadcopterFrameIR ir;
    ir.parameters_ = defaults();

    if (raw_parameters.is_object()) {
        for (const auto& [key, value] : raw_parameters.as_object()) {
            const auto it = ir.parameters_.find(key);
            if (it == ir.parameters_.end()) {
                core::Json details = core::Json::object();
                core::Json allowed = core::Json::array();
                for (const auto& [name, v] : defaults()) {
                    (void)v;
                    allowed.push_back(core::Json(name));
                }
                details["allowed"] = allowed;
                reject("Unknown quadcopter_frame parameter '" + key + "'", details);
            }
            const double numeric = value.as_double();
            if (!std::isfinite(numeric)) reject(key + " must be a finite positive value");
            it->second = numeric;
        }
    }

    const double overall = ir.parameters_["overall_size"];
    const double plate = ir.parameters_["center_plate_size"];
    const double arm_width = ir.parameters_["arm_width"];
    const double thickness = ir.parameters_["plate_thickness"];
    const double mount = ir.parameters_["motor_mount_diameter"];
    const double fc_spacing = ir.parameters_["fc_mount_spacing"];
    const double motor_count = ir.parameters_["motor_count"];

    if (motor_count != 4.0) {
        core::Json details = core::Json::object();
        details["motor_count"] = motor_count;
        reject("V1 only supports 4-motor (X) quadcopter frames", details);
    }
    for (const char* key :
         {"overall_size", "center_plate_size", "arm_width", "plate_thickness",
          "motor_mount_diameter", "fc_mount_spacing"}) {
        const double v = ir.parameters_[key];
        if (!std::isfinite(v) || v <= 0.0) {
            reject(std::string(key) + " must be a finite positive value");
        }
    }
    if (overall <= plate) {
        core::Json details = core::Json::object();
        details["overall_size"] = overall;
        details["center_plate_size"] = plate;
        reject("overall_size must be larger than center_plate_size", details);
    }
    if (overall > 1000.0 || thickness > 50.0) {
        reject("Frame dimensions are outside V1's supported engineering range");
    }
    if (fc_spacing > plate) {
        reject("fc_mount_spacing must fit within center_plate_size");
    }
    return ir;
}

core::Json QuadcopterFrameIR::to_json() const {
    core::Json out = core::Json::object();
    out["type"] = "quadcopter_frame";
    out["units"] = units();
    core::Json params = core::Json::object();
    for (const auto& [key, value] : parameters_) {
        params[key] = value;
    }
    out["parameters"] = params;
    return out;
}

Mesh build_quadcopter_frame(const QuadcopterFrameIR& ir) {
    const std::map<std::string, double>& p = ir.parameters();
    const double plate_size = p.at("center_plate_size");
    const double thickness = p.at("plate_thickness");
    const double arm_width = p.at("arm_width");
    const double overall = p.at("overall_size");
    const double mount_d = p.at("motor_mount_diameter");

    Mesh mesh = make_box({0.0, 0.0, 0.0}, {plate_size, plate_size, thickness});

    const double start_r = (plate_size / 2.0) * std::sqrt(2.0);
    const double end_r = overall / 2.0;
    const double arm_length = end_r - start_r;
    const double center_r = (start_r + end_r) / 2.0;
    const double boss_side = mount_d * 1.4;

    const double angles[4] = {45.0, 135.0, 225.0, 315.0};  // X configuration
    for (const double angle : angles) {
        const double theta = angle * kPi / 180.0;
        const double arm_x = center_r * std::cos(theta);
        const double arm_y = center_r * std::sin(theta);
        mesh.extend(make_box({arm_x, arm_y, 0.0}, {arm_length, arm_width, thickness}, angle));

        const double motor_x = end_r * std::cos(theta);
        const double motor_y = end_r * std::sin(theta);
        mesh.extend(make_box({motor_x, motor_y, 0.0}, {boss_side, boss_side, thickness * 1.5},
                             angle));
    }
    return mesh;
}

ValidationReport validate_quadcopter_frame(const QuadcopterFrameIR& ir, const Mesh& mesh) {
    const std::map<std::string, double>& p = ir.parameters();
    ValidationReport report;
    core::Json checks = core::Json::object();
    bool ok = true;

    // --- dimensions -------------------------------------------------------
    const auto [min_c, max_c] = mesh.bounding_box();
    const double span_x = std::get<0>(max_c) - std::get<0>(min_c);
    const double span_y = std::get<1>(max_c) - std::get<1>(min_c);
    const double boss_side = p.at("motor_mount_diameter") * 1.4;
    const double expected = std::sqrt(2.0) * (p.at("overall_size") / 2.0 + boss_side);
    const double tolerance = expected * 0.15;
    const bool dims_ok = std::fabs(span_x - expected) <= tolerance &&
                         std::fabs(span_y - expected) <= tolerance;
    checks["dimensions_within_tolerance"] = dims_ok;
    checks["expected_span_mm"] = std::round(expected * 1000.0) / 1000.0;
    {
        core::Json actual = core::Json::array();
        actual.push_back(std::round(span_x * 1000.0) / 1000.0);
        actual.push_back(std::round(span_y * 1000.0) / 1000.0);
        checks["actual_span_mm"] = actual;
    }
    ok = ok && dims_ok;

    // --- topology / finiteness -------------------------------------------
    bool finite_ok = true;
    int degenerate = 0;
    for (const Triangle& triangle : mesh.triangles) {
        for (const Vec3& vertex : {std::get<0>(triangle), std::get<1>(triangle),
                                   std::get<2>(triangle)}) {
            const auto [x, y, z] = vertex;
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) finite_ok = false;
        }
        const auto& [v0, v1, v2] = triangle;
        const auto [x0, y0, z0] = v0;
        const auto [x1, y1, z1] = v1;
        const auto [x2, y2, z2] = v2;
        const double ux = x1 - x0, uy = y1 - y0, uz = z1 - z0;
        const double vx = x2 - x0, vy = y2 - y0, vz = z2 - z0;
        const double cx = uy * vz - uz * vy;
        const double cy = uz * vx - ux * vz;
        const double cz = ux * vy - uy * vx;
        if (0.5 * std::sqrt(cx * cx + cy * cy + cz * cz) < 1e-9) ++degenerate;
    }
    checks["all_vertices_finite"] = finite_ok;
    checks["degenerate_triangle_count"] = static_cast<double>(degenerate);
    ok = ok && finite_ok && degenerate == 0;

    // --- clearances --------------------------------------------------------
    const double start_r = (p.at("center_plate_size") / 2.0) * std::sqrt(2.0);
    const double arm_length = p.at("overall_size") / 2.0 - start_r;
    const bool clearance_ok = arm_length > 0.0;
    checks["arm_length_mm"] = std::round(arm_length * 1000.0) / 1000.0;
    checks["arm_length_positive"] = clearance_ok;
    ok = ok && clearance_ok;

    // --- manufacturability -------------------------------------------------
    const bool manufacturable = p.at("arm_width") >= kMinPrintableFeatureMm &&
                                p.at("plate_thickness") >= kMinPrintableFeatureMm;
    checks["manufacturable_min_feature"] = manufacturable;
    ok = ok && manufacturable;

    checks["triangle_count"] = static_cast<double>(mesh.triangle_count());
    report.passed = ok;
    report.checks = checks;
    return report;
}

}  // namespace trinity::engines::cad
