// Trinity — Quadcopter frame CAD IR + deterministic builder + validators.
// Port of backend/app/engines/cad/{ir,builder,validators}.py with identical
// parameters, geometry and validation checks (asserted by tests).
#pragma once

#include <map>
#include <string>

#include "../../core/Json.hpp"
#include "Mesh.hpp"

namespace trinity::engines::cad {

// --------------------------------------------------------------- IR

// Default parameters mirror DEFAULT_QUADCOPTER_PARAMS in ir.py.
class QuadcopterFrameIR {
public:
    // Validates and normalises raw parameters; throws core::TrinityException
    // (request_validation_error) with the same conditions as the Python IR:
    // unknown keys rejected, motor_count == 4, positive finite values,
    // overall_size > center_plate_size, fc_mount_spacing fits plate,
    // V1 range limits (overall <= 1000 mm, plate_thickness <= 50 mm).
    static QuadcopterFrameIR from_request(const core::Json& raw_parameters);

    const std::map<std::string, double>& parameters() const { return parameters_; }
    std::string units() const { return "mm"; }
    core::Json to_json() const;

private:
    QuadcopterFrameIR() = default;
    std::map<std::string, double> parameters_;
};

// ------------------------------------------------------------- builder

// Deterministic X-configuration frame: center plate + 4 arms at
// 45/135/225/315 degrees + motor bosses. Identical arithmetic to builder.py.
Mesh build_quadcopter_frame(const QuadcopterFrameIR& ir);

// ------------------------------------------------------------ validation

// Port of validators.py: dimension tolerance (15% of expected diagonal span),
// finite vertices, no degenerate triangles, positive arm length, FDM minimum
// feature size >= 1.0 mm. Returns pass/fail plus the individual checks.
struct ValidationReport {
    bool passed = false;
    core::Json checks = core::Json::object();
};

ValidationReport validate_quadcopter_frame(const QuadcopterFrameIR& ir, const Mesh& mesh);

}  // namespace trinity::engines::cad
