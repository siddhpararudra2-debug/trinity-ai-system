#include "trinity/cad/Validators.hpp"

#include <cmath>

namespace trinity::cad {

FrameValidation validateQuadcopterFrame(const FrameParams& params, const Mesh& mesh) {
    FrameValidation result;
    const auto& p = params.parameters;
    core::Json checks = core::Json::object();
    bool ok = true;

    // Dimensions: bounding-box span vs the projected diagonal footprint.
    const BoundingBox bbox = mesh.boundingBox();
    const double spanX = bbox.spanX();
    const double spanY = bbox.spanY();
    const double bossSide = p.at("motor_mount_diameter") * 1.4;
    const double expected = std::sqrt(2.0) * (p.at("overall_size") / 2.0 + bossSide);
    const double tolerance = expected * 0.15;
    const bool dimsOk = std::fabs(spanX - expected) <= tolerance &&
                        std::fabs(spanY - expected) <= tolerance;
    checks["dimensions_within_tolerance"] = dimsOk;
    checks["expected_span_mm"] = expected;
    checks["actual_span_mm"] = {spanX, spanY};
    ok = ok && dimsOk;

    // Finiteness + non-degeneracy.
    bool finiteOk = true;
    int degenerate = 0;
    for (const auto& tri : mesh.triangles()) {
        for (const auto& v : tri) {
            for (double c : v) {
                if (std::isnan(c) || std::isinf(c)) {
                    finiteOk = false;
                }
            }
        }
        if (triangleArea(tri) < 1e-9) {
            ++degenerate;
        }
    }
    checks["all_vertices_finite"] = finiteOk;
    checks["degenerate_triangle_count"] = degenerate;
    ok = ok && finiteOk && degenerate == 0;

    // Clearances: arms must span a positive length.
    const double startR = (p.at("center_plate_size") / 2.0) * std::sqrt(2.0);
    const double endR = p.at("overall_size") / 2.0;
    const double armLength = endR - startR;
    const bool clearanceOk = armLength > 0.0;
    checks["arm_length_mm"] = armLength;
    checks["arm_length_positive"] = clearanceOk;
    ok = ok && clearanceOk;

    // Manufacturability: FDM minimum feature floor.
    const bool manufacturable = p.at("arm_width") >= kMinPrintableFeatureMm &&
                                p.at("plate_thickness") >= kMinPrintableFeatureMm;
    checks["manufacturable_min_feature"] = manufacturable;
    ok = ok && manufacturable;

    checks["triangle_count"] = static_cast<int>(mesh.triangleCount());
    result.ok = ok;
    result.checks = std::move(checks);
    return result;
}

}  // namespace trinity::cad
