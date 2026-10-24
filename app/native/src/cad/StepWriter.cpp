#include "trinity/cad/StepWriter.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace trinity::cad {
namespace {

// STEP real literals need a decimal point or an exponent: format with
// %.10g, normalize the exponent marker, and fix bare integers to "n.0" so
// every value parses strictly in all readers.
std::string stepReal(double value) {
    if (!std::isfinite(value)) {
        throw std::runtime_error(
            "STEP cannot represent non-finite coordinates");
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", value);
    std::string out(buf);
    for (char& c : out) {
        if (c == 'e') {
            c = 'E';
        }
    }
    if (out.find_first_of(".E") == std::string::npos) {
        out += ".0";
    }
    return out;
}

std::string stepString(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string isoTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return std::string(buf);
}

// Sequential deterministic id allocator: every entity is emitted before
// any reference to it, so the file stays free of forward references.
struct EntityBuilder {
    int nextId = 0;
    std::ostringstream body;

    int emit(const std::string& text) {
        const int id = ++nextId;
        body << '#' << id << '=' << text << ";\n";
        return id;
    }
};

struct VertexInfo {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int pointId = 0;   // CARTESIAN_POINT
    int vertexId = 0;  // VERTEX_POINT
};

constexpr int kVertexQuantum = 1000000;

std::array<long long, 3> quantize(const Vec3& point) {
    std::array<long long, 3> key{};
    for (size_t axis = 0; axis < 3; ++axis) {
        key[axis] = std::llround(point[axis] * kVertexQuantum);
    }
    return key;
}

}  // namespace

std::string encodeStep(const Mesh& mesh, const std::string& productName) {
    if (mesh.empty()) {
        throw std::runtime_error("STEP export needs a non-empty mesh");
    }

    std::ostringstream header;
    header << "ISO-10303-21;\n";
    header << "HEADER;\n";
    header << "FILE_DESCRIPTION(("
           << stepString("Faceted solid from tessellated triangles") << ","
           << stepString("exported by Trinity native CAD") << "),'2;1');\n";
    header << "FILE_NAME(" << stepString(productName + ".step") << ","
           << stepString(isoTimestamp()) << "," << stepString("Trinity AI")
           << "," << stepString("trinity-ai-system") << ","
           << stepString("Trinity native StepWriter") << ",'');\n";
    header << "FILE_SCHEMA((AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 4 3 }));\n";
    header << "ENDSEC;\nDATA;\n";

    EntityBuilder sb;

    // Units + geometric context (millimetres, radians, steradians).
    const int unitLength = sb.emit(
        "( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )");
    const int unitAngle = sb.emit(
        "( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )");
    const int unitSolid = sb.emit(
        "( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) PLANE_ANGLE_UNIT() )");
    const int uncertainty = sb.emit(
        "UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#" +
        std::to_string(unitLength) +
        ",'distance_accuracy_value','confusion accuracy')");
    const int context3d = sb.emit(
        "( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
        "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#" +
        std::to_string(uncertainty) + ")) GLOBAL_UNIT_ASSIGNED_CONTEXT((#" +
        std::to_string(unitLength) + ",#" + std::to_string(unitAngle) + ",#" +
        std::to_string(unitSolid) +
        ")) REPRESENTATION_CONTEXT('Context #1'," +
        stepString("3D Context with UNIT and uncertainty") + ") )");

    // Product chain.
    const int appContext = sb.emit(
        "APPLICATION_CONTEXT('core data for automotive mechanical design "
        "processes')");
    sb.emit("APPLICATION_PROTOCOL_DEFINITION('international standard',"
            "'automotive_design',2010,#" +
            std::to_string(appContext) + ")");
    const int productContext = sb.emit("PRODUCT_CONTEXT('',#" +
                                       std::to_string(appContext) +
                                       ",'mechanical')");
    const int productId =
        sb.emit(std::string("PRODUCT(") + stepString(productName) + "," +
                stepString(productName) + ",'',(#" +
                std::to_string(productContext) + "))");
    const int formation = sb.emit("PRODUCT_DEFINITION_FORMATION('','',#" +
                                  std::to_string(productId) + ")");
    const int definitionContext = sb.emit(
        "PRODUCT_DEFINITION_CONTEXT('part definition',#" +
        std::to_string(appContext) + ",'design')");
    const int productDefinition =
        sb.emit("PRODUCT_DEFINITION('design','',#" +
                std::to_string(formation) + ",#" +
                std::to_string(definitionContext) + ")");
    const int definitionShape =
        sb.emit("PRODUCT_DEFINITION_SHAPE('','',#" +
                std::to_string(productDefinition) + ")");

    // Global placement of the shape representation.
    const int originPoint = sb.emit("CARTESIAN_POINT('',(0.,0.,0.))");
    const int unitZ = sb.emit("DIRECTION('',(0.,0.,1.))");
    const int unitX = sb.emit("DIRECTION('',(1.,0.,0.))");
    const int globalPlacement =
        sb.emit("AXIS2_PLACEMENT_3D('',#" + std::to_string(originPoint) +
                ",#" + std::to_string(unitZ) + ",#" + std::to_string(unitX) +
                ")");

    // Shared vertices: one CARTESIAN_POINT + VERTEX_POINT per topological
    // point. Dedup key matches the watertight check in Csg.cpp exactly
    // (1e-6 mm quantization).
    std::map<std::array<long long, 3>, VertexInfo> vertices;
    for (const Triangle& tri : mesh.triangles()) {
        for (size_t i = 0; i < 3; ++i) {
            const Vec3& point = tri[i];
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) ||
                !std::isfinite(point[2])) {
                throw std::runtime_error(
                    "STEP export needs finite coordinates");
            }
            const auto key = quantize(point);
            if (vertices.find(key) != vertices.end()) {
                continue;
            }
            VertexInfo info;
            info.x = point[0];
            info.y = point[1];
            info.z = point[2];
            info.pointId = sb.emit(
                std::string("CARTESIAN_POINT('',(") + stepReal(point[0]) +
                "," + stepReal(point[1]) + "," + stepReal(point[2]) + "))");
            info.vertexId =
                sb.emit("VERTEX_POINT('',#" + std::to_string(info.pointId) +
                        ")");
            vertices.emplace(key, info);
        }
    }

    // One FACE_SURFACE + PLANE per triangle, with a POLY_LOOP outer bound
    // over the triangle's three shared VERTEX_POINTs.
    std::vector<int> faceIds;
    faceIds.reserve(mesh.triangleCount());
    for (const Triangle& tri : mesh.triangles()) {
        const Vec3 normal = triangleNormal(tri);
        const double len = std::sqrt(normal[0] * normal[0] +
                                     normal[1] * normal[1] +
                                     normal[2] * normal[2]);
        if (len < 1e-12) {
            throw std::runtime_error(
                "STEP export needs non-degenerate triangles");
        }

        std::array<int, 3> vertexRefs{};
        std::array<int, 3> pointRefs{};
        for (size_t i = 0; i < 3; ++i) {
            const auto key = quantize(tri[i]);
            const auto it = vertices.find(key);
            if (it == vertices.end() || it->second.vertexId == 0) {
                throw std::runtime_error("STEP vertex bookkeeping failure");
            }
            vertexRefs[i] = it->second.vertexId;
            pointRefs[i] = it->second.pointId;
        }
        if (vertexRefs[0] == vertexRefs[1] ||
            vertexRefs[1] == vertexRefs[2] ||
            vertexRefs[2] == vertexRefs[0]) {
            throw std::runtime_error(
                "STEP export needs non-degenerate triangles");
        }

        // Plane placement: z = outward normal; x = deterministic
        // perpendicular unit vector (Gram-Schmidt off a safe seed axis).
        Vec3 xdir{1.0, 0.0, 0.0};
        if (std::fabs(normal[0]) > 0.9) {
            xdir = Vec3{0.0, 1.0, 0.0};
        }
        const double proj = normal[0] * xdir[0] + normal[1] * xdir[1] +
                            normal[2] * xdir[2];
        xdir = Vec3{xdir[0] - normal[0] * proj, xdir[1] - normal[1] * proj,
                    xdir[2] - normal[2] * proj};
        const double xlen = std::sqrt(xdir[0] * xdir[0] +
                                       xdir[1] * xdir[1] +
                                       xdir[2] * xdir[2]);
        if (xlen < 1e-12) {
            throw std::runtime_error(
                "STEP plane placement failed to find an x axis");
        }
        xdir = Vec3{xdir[0] / xlen, xdir[1] / xlen, xdir[2] / xlen};

        const int dirZ = sb.emit(std::string("DIRECTION('',(") +
                                 stepReal(normal[0]) + "," +
                                 stepReal(normal[1]) + "," +
                                 stepReal(normal[2]) + "))");
        const int dirX =
            sb.emit(std::string("DIRECTION('',(") + stepReal(xdir[0]) + "," +
                    stepReal(xdir[1]) + "," + stepReal(xdir[2]) + "))");
        // Plane origin: the first corner's shared CARTESIAN_POINT.
        const int planeOrigin = pointRefs[0];
        const int planeAxis =
            sb.emit("AXIS2_PLACEMENT_3D('',#" + std::to_string(planeOrigin) +
                    ",#" + std::to_string(dirZ) + ",#" +
                    std::to_string(dirX) + ")");
        const int planeId = sb.emit("PLANE('',#" +
                                    std::to_string(planeAxis) + ")");
        const std::string loop = std::string("POLY_LOOP('',(#") +
                                 std::to_string(vertexRefs[0]) + ",#" +
                                 std::to_string(vertexRefs[1]) + ",#" +
                                 std::to_string(vertexRefs[2]) + "))";
        const int loopId = sb.emit(loop);
        const int boundId = sb.emit("FACE_OUTER_BOUND('',#" +
                                    std::to_string(loopId) + ",.T.)");
        const int faceId =
            sb.emit(std::string("FACE_SURFACE('',#") +
                    std::to_string(boundId) + ",#" +
                    std::to_string(planeId) + ",.T.)");
        faceIds.push_back(faceId);
    }

    // Shell + brep + representation chain.
    std::string shellFaces;
    for (size_t i = 0; i < faceIds.size(); ++i) {
        if (i != 0) {
            shellFaces += ",";
        }
        shellFaces += "#" + std::to_string(faceIds[i]);
    }
    const int shellId =
        sb.emit(std::string("CLOSED_SHELL('',(") + shellFaces + "))");
    const int brepId =
        sb.emit(std::string("FACETED_BREP(") + stepString(productName) +
                ",#" + std::to_string(shellId) + ",.T.)");
    const int brepRep = sb.emit(
        "ADVANCED_BREP_SHAPE_REPRESENTATION('',(#" +
        std::to_string(globalPlacement) + "),(#" + std::to_string(brepId) +
        "),#" + std::to_string(context3d) + ")");
    sb.emit("SHAPE_DEFINITION_REPRESENTATION(#" +
            std::to_string(definitionShape) + ",#" +
            std::to_string(brepRep) + ")");

    header << sb.body.str();
    header << "ENDSEC;\nEND-ISO-10303-21;\n";
    return header.str();
}

void writeStep(const Mesh& mesh, const std::string& path,
               const std::string& productName) {
    const std::string content = encodeStep(mesh, productName);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Cannot open STEP output file: " + path);
    }
    file << content;
    file.close();
    if (!file) {
        throw std::runtime_error("Failed while writing STEP output: " + path);
    }
}

}  // namespace trinity::cad
