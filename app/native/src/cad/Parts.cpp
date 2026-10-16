#include "trinity/cad/Parts.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "trinity/cad/Builder.hpp"
#include "trinity/cad/FrameParams.hpp"
#include "trinity/core/Error.hpp"

namespace trinity::cad {

core::Json PartSpec::toJson() const {
    core::Json paramsJson = core::Json::object();
    for (const auto& [key, value] : parameters) {
        paramsJson[key] = value;
    }
    return core::Json{{"type", type}, {"units", units}, {"parameters", paramsJson}};
}

namespace {

// Python :g formatting (6 significant digits, trailing zeros stripped)
// with '.' mapped to 'p' so artifact stems stay dot-free.
std::string formatG(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", value);
    std::string out(buf);
    for (char& c : out) {
        if (c == '.') {
            c = 'p';
        }
    }
    return out;
}

void checkBounds(const std::string& part,
                 const std::map<std::string, ParamBound>& bounds,
                 std::map<std::string, double>& params) {
    (void)part;
    for (const auto& [key, bound] : bounds) {
        const double value = params.at(key);
        const std::string u = bound.unit;
        if (bound.minExclusive) {
            if (!std::isfinite(value) || value <= bound.min) {
                if (bound.min == 0.0) {
                    throw core::RequestValidationError(
                        key + " must be a finite positive value",
                        {{key, value}}, "engines");
                }
                throw core::RequestValidationError(
                    key + " must be greater than " + formatG(bound.min) + " " + u,
                    {{key, value}}, "engines");
            }
        } else if (!std::isfinite(value) || value < bound.min) {
            throw core::RequestValidationError(
                key + " must be a finite value of at least " + formatG(bound.min) +
                    " " + u,
                {{key, value}}, "engines");
        }
        if (bound.hasMax && value > bound.max) {
            throw core::RequestValidationError(
                key + " must be at most " + formatG(bound.max) + " " + u,
                {{key, value}}, "engines");
        }
        if (bound.integer && std::floor(value) != value) {
            throw core::RequestValidationError(key + " must be an integer",
                                               {{key, value}}, "engines");
        }
    }
}

PartSpec mergeDefaults(const std::string& type,
                       const std::map<std::string, double>& defaults,
                       const std::map<std::string, ParamBound>& bounds,
                       const core::Json& raw) {
    PartSpec spec;
    spec.type = type;
    spec.units = "mm";
    spec.parameters = defaults;
    if (!raw.is_null() && !raw.is_object()) {
        throw core::RequestValidationError("CAD parameters must be an object",
                                           {{"parameters", raw}}, "engines");
    }
    if (!raw.is_null()) {
        for (auto it = raw.begin(); it != raw.end(); ++it) {
            if (spec.parameters.find(it.key()) == spec.parameters.end()) {
                core::Json allowed = core::Json::array();
                for (const auto& [key, _] : spec.parameters) {
                    allowed.push_back(key);
                }
                throw core::RequestValidationError(
                    "Unknown " + type + " parameter '" + it.key() + "'",
                    {{"allowed", allowed}}, "engines");
            }
            if (!it.value().is_number()) {
                throw core::RequestValidationError(
                    "Parameter '" + it.key() + "' must be numeric",
                    {{"parameter", it.key()}}, "engines");
            }
            spec.parameters[it.key()] = it.value().get<double>();
        }
    }
    checkBounds(type, bounds, spec.parameters);
    return spec;
}

struct MeshIntegrity {
    bool finite = true;
    int degenerate = 0;
    int triangles = 0;
};

MeshIntegrity integrity(const Mesh& mesh) {
    MeshIntegrity out;
    out.triangles = static_cast<int>(mesh.triangleCount());
    for (const auto& tri : mesh.triangles()) {
        for (const auto& v : tri) {
            for (double c : v) {
                if (std::isnan(c) || std::isinf(c)) {
                    out.finite = false;
                }
            }
        }
        if (triangleArea(tri) < 1e-9) {
            ++out.degenerate;
        }
    }
    return out;
}

bool envelopesMatch(const Mesh& mesh, const Vec3& eMin, const Vec3& eMax) {
    constexpr double kTolMm = 1e-6;
    const BoundingBox bbox = mesh.boundingBox();
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(bbox.min[i] - eMin[i]) > kTolMm) {
            return false;
        }
        if (std::fabs(bbox.max[i] - eMax[i]) > kTolMm) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------- quadcopter_frame ---

PartSpec quadFromRequest(const core::Json& raw) {
    const FrameParams params = FrameParams::fromRequest(raw);
    PartSpec spec;
    spec.type = "quadcopter_frame";
    spec.units = params.units;
    spec.parameters = params.parameters;
    return spec;
}

Mesh quadBuilder(const PartSpec& spec) {
    FrameParams params;
    params.units = spec.units;
    params.parameters = spec.parameters;
    return buildQuadcopterFrame(params);
}

FrameValidation quadValidator(const PartSpec& spec, const Mesh& mesh) {
    FrameParams params;
    params.units = spec.units;
    params.parameters = spec.parameters;
    return validateQuadcopterFrame(params, mesh);
}

std::string quadStem(const PartSpec& spec) {
    const int overallMm = static_cast<int>(spec.parameters.at("overall_size"));
    return "quadcopter_frame_" + std::to_string(overallMm) + "mm";
}

// ------------------------------------------------------------ enclosure ---

const std::map<std::string, double> kEnclosureDefaults = {
    {"length", 100.0}, {"width", 60.0}, {"height", 40.0}, {"wall_thickness", 2.0},
};

const std::map<std::string, ParamBound> kEnclosureBounds = {
    {"length", {0.0, true, true, 1000.0, false, "mm"}},
    {"width", {0.0, true, true, 1000.0, false, "mm"}},
    {"height", {0.0, true, true, 1000.0, false, "mm"}},
    {"wall_thickness", {1.0, false, true, 50.0, false, "mm"}},
};

PartSpec enclosureFromRequest(const core::Json& raw) {
    PartSpec spec = mergeDefaults("enclosure", kEnclosureDefaults, kEnclosureBounds, raw);
    auto& p = spec.parameters;
    const double wall = p.at("wall_thickness");
    if (2.0 * wall >= std::min(p.at("length"), p.at("width"))) {
        throw core::RequestValidationError(
            "2 x wall_thickness must be less than the smaller of length and width",
            {{"wall_thickness", wall},
             {"length", p.at("length")},
             {"width", p.at("width")}},
            "engines");
    }
    if (wall >= p.at("height")) {
        throw core::RequestValidationError(
            "wall_thickness must be less than height",
            {{"wall_thickness", wall}, {"height", p.at("height")}}, "engines");
    }
    return spec;
}

Mesh buildEnclosure(const PartSpec& spec) {
    const auto& p = spec.parameters;
    const double length = p.at("length");
    const double width = p.at("width");
    const double height = p.at("height");
    const double wall = p.at("wall_thickness");
    Mesh mesh = box({0.0, 0.0, wall / 2.0}, {length, width, wall});
    for (double sign : {1.0, -1.0}) {
        mesh.extend(box({0.0, sign * (width / 2.0 - wall / 2.0), height / 2.0},
                        {length, wall, height}));
    }
    for (double sign : {1.0, -1.0}) {
        mesh.extend(box({sign * (length / 2.0 - wall / 2.0), 0.0, height / 2.0},
                        {wall, width - 2.0 * wall, height}));
    }
    return mesh;
}

FrameValidation validateEnclosure(const PartSpec& spec, const Mesh& mesh) {
    FrameValidation result;
    const auto& p = spec.parameters;
    core::Json checks = core::Json::object();
    const bool dimsOk = envelopesMatch(
        mesh, {-p.at("length") / 2.0, -p.at("width") / 2.0, 0.0},
        {p.at("length") / 2.0, p.at("width") / 2.0, p.at("height")});
    checks["dimensions_match_envelope"] = dimsOk;
    const MeshIntegrity info = integrity(mesh);
    checks["all_vertices_finite"] = info.finite;
    checks["degenerate_triangle_count"] = info.degenerate;
    const bool manufacturable =
        p.at("wall_thickness") >= kMinPrintableFeatureMm;
    checks["manufacturable_min_feature"] = manufacturable;
    checks["triangle_count"] = info.triangles;
    result.ok = dimsOk && info.finite && info.degenerate == 0 && manufacturable;
    result.checks = std::move(checks);
    return result;
}

std::string enclosureStem(const PartSpec& spec) {
    const auto& p = spec.parameters;
    return "enclosure_" + formatG(p.at("length")) + "x" + formatG(p.at("width")) +
           "x" + formatG(p.at("height")) + "mm";
}

// ------------------------------------------------------------ l_bracket ---

const std::map<std::string, double> kLBracketDefaults = {
    {"base_length", 40.0}, {"height", 40.0}, {"width", 20.0}, {"thickness", 3.0},
};

const std::map<std::string, ParamBound> kLBracketBounds = {
    {"base_length", {0.0, true, true, 1000.0, false, "mm"}},
    {"height", {0.0, true, true, 1000.0, false, "mm"}},
    {"width", {0.0, true, true, 1000.0, false, "mm"}},
    {"thickness", {1.0, false, true, 50.0, false, "mm"}},
};

PartSpec lBracketFromRequest(const core::Json& raw) {
    PartSpec spec = mergeDefaults("l_bracket", kLBracketDefaults, kLBracketBounds, raw);
    auto& p = spec.parameters;
    const double thickness = p.at("thickness");
    if (thickness >= p.at("base_length")) {
        throw core::RequestValidationError(
            "thickness must be less than base_length",
            {{"thickness", thickness}, {"base_length", p.at("base_length")}},
            "engines");
    }
    if (thickness >= p.at("height")) {
        throw core::RequestValidationError(
            "thickness must be less than height",
            {{"thickness", thickness}, {"height", p.at("height")}}, "engines");
    }
    return spec;
}

Mesh buildLBracket(const PartSpec& spec) {
    const auto& p = spec.parameters;
    const double baseLength = p.at("base_length");
    const double height = p.at("height");
    const double width = p.at("width");
    const double thickness = p.at("thickness");
    Mesh mesh = box({0.0, 0.0, thickness / 2.0}, {baseLength, width, thickness});
    mesh.extend(box({-baseLength / 2.0 + thickness / 2.0, 0.0, height / 2.0},
                    {thickness, width, height}));
    return mesh;
}

FrameValidation validateLBracket(const PartSpec& spec, const Mesh& mesh) {
    FrameValidation result;
    const auto& p = spec.parameters;
    core::Json checks = core::Json::object();
    const bool dimsOk = envelopesMatch(
        mesh, {-p.at("base_length") / 2.0, -p.at("width") / 2.0, 0.0},
        {p.at("base_length") / 2.0, p.at("width") / 2.0, p.at("height")});
    checks["dimensions_match_envelope"] = dimsOk;
    const MeshIntegrity info = integrity(mesh);
    checks["all_vertices_finite"] = info.finite;
    checks["degenerate_triangle_count"] = info.degenerate;
    const bool manufacturable = p.at("thickness") >= kMinPrintableFeatureMm;
    checks["manufacturable_min_feature"] = manufacturable;
    checks["triangle_count"] = info.triangles;
    result.ok = dimsOk && info.finite && info.degenerate == 0 && manufacturable;
    result.checks = std::move(checks);
    return result;
}

std::string lBracketStem(const PartSpec& spec) {
    const auto& p = spec.parameters;
    return "l_bracket_" + formatG(p.at("base_length")) + "x" +
           formatG(p.at("height")) + "x" + formatG(p.at("width")) + "mm";
}

}  // namespace

const std::vector<PartDefinition>& partRegistry() {
    static const std::vector<PartDefinition> registry = {
        {"enclosure", "Open-top enclosure",
         "Open-top rectangular enclosure with a floor and four walls.",
         kEnclosureDefaults, kEnclosureBounds,
         {"2 x wall_thickness must be less than the smaller of length and width",
          "wall_thickness must be less than height"},
         enclosureFromRequest, buildEnclosure, validateEnclosure, enclosureStem},
        {"l_bracket", "L-bracket",
         "Right-angle bracket: a flat base with one upright wall.",
         kLBracketDefaults, kLBracketBounds,
         {"thickness must be less than base_length",
          "thickness must be less than height"},
         lBracketFromRequest, buildLBracket, validateLBracket, lBracketStem},
        {"quadcopter_frame", "Quadcopter frame",
         "X-configuration quadcopter frame with center plate and four arms.",
         defaultFrameParams(), {},
         {"overall_size must be greater than center_plate_size",
          "motor_count must be 4"},
         quadFromRequest, quadBuilder, quadValidator, quadStem},
    };
    return registry;
}

const PartDefinition* findPart(const std::string& name) {
    for (const auto& part : partRegistry()) {
        if (part.name == name) {
            return &part;
        }
    }
    return nullptr;
}

std::vector<std::string> partNames() {
    std::vector<std::string> names;
    for (const auto& part : partRegistry()) {
        names.push_back(part.name);
    }
    return names;
}

}  // namespace trinity::cad
