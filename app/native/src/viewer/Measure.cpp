#include "trinity/viewer/Measure.hpp"

#include <cmath>

namespace trinity::viewer {

double measureDistance(const cad::Vec3& a, const cad::Vec3& b) noexcept {
    const double dx = b[0] - a[0];
    const double dy = b[1] - a[1];
    const double dz = b[2] - a[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

cad::Vec3 measureDelta(const cad::Vec3& a, const cad::Vec3& b) noexcept {
    return {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
}

}  // namespace trinity::viewer
