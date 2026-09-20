#include "trinity/cad/Builder.hpp"

#include <cmath>

namespace trinity::cad {

const double kArmAnglesDeg[4] = {45.0, 135.0, 225.0, 315.0};

Mesh buildQuadcopterFrame(const FrameParams& params) {
    const auto& p = params.parameters;
    const double plateSize = p.at("center_plate_size");
    const double thickness = p.at("plate_thickness");
    const double armWidth = p.at("arm_width");
    const double overall = p.at("overall_size");
    const double mountD = p.at("motor_mount_diameter");

    constexpr double kPi = 3.14159265358979323846;
    Mesh mesh = box({0.0, 0.0, 0.0}, {plateSize, plateSize, thickness});

    const double startR = (plateSize / 2.0) * std::sqrt(2.0);
    const double endR = overall / 2.0;
    const double armLength = endR - startR;

    for (double angle : kArmAnglesDeg) {
        const double theta = angle * kPi / 180.0;
        const double centerR = (startR + endR) / 2.0;
        mesh.extend(box({centerR * std::cos(theta), centerR * std::sin(theta), 0.0},
                        {armLength, armWidth, thickness}, angle));

        const double bossSide = mountD * 1.4;
        mesh.extend(box({endR * std::cos(theta), endR * std::sin(theta), 0.0},
                        {bossSide, bossSide, thickness * 1.5}, angle));
    }
    return mesh;
}

}  // namespace trinity::cad
