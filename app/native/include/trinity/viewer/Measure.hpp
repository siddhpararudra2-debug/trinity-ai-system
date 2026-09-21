#pragma once

// Basic measurement over actual model coordinates (Qt-free).
// Advanced CAD dimensioning is explicitly out of scope.

#include "../cad/Mesh.hpp"

namespace trinity::viewer {

struct Measurement {
    cad::Vec3 a{0.0, 0.0, 0.0};
    cad::Vec3 b{0.0, 0.0, 0.0};
    bool hasA = false;
    bool hasB = false;

    bool complete() const noexcept { return hasA && hasB; }
    void clear() noexcept { hasA = hasB = false; }
};

double measureDistance(const cad::Vec3& a, const cad::Vec3& b) noexcept;
cad::Vec3 measureDelta(const cad::Vec3& a, const cad::Vec3& b) noexcept;

}  // namespace trinity::viewer
