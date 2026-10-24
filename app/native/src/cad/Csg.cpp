#include "trinity/cad/Csg.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trinity::cad {
namespace {

// Absolute epsilon at mm scale: part dimensions run 1..100 mm, and a fixed
// epsilon keeps every split decision deterministic run to run.
constexpr double kEpsilon = 1e-5;

double dot3(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 crossProduct(const Vec3& a, const Vec3& b) {
    return Vec3{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
}

Vec3 sub3(const Vec3& a, const Vec3& b) {
    return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

Vec3 add3(const Vec3& a, const Vec3& b) {
    return Vec3{a[0] + b[0], a[1] + b[1], a[2] + b[2]};
}

Vec3 mul3(const Vec3& a, double s) {
    return Vec3{a[0] * s, a[1] * s, a[2] * s};
}

double length3(const Vec3& a) {
    return std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
}

enum class VtType { Front, Back, Coincident };

struct Plane {
    Vec3 normal{0.0, 0.0, 0.0};
    double w = 0.0;

    static Plane fromVertices(const std::vector<Vec3>& vertices) {
        // Newell normal: robust for planar vertex loops and numerically
        // stable for near-degenerate triangles.
        Vec3 n{0.0, 0.0, 0.0};
        const size_t count = vertices.size();
        for (size_t i = 0; i < count; ++i) {
            const Vec3& current = vertices[i];
            const Vec3& next = vertices[(i + 1) % count];
            n[0] += (current[1] - next[1]) * (current[2] + next[2]);
            n[1] += (current[2] - next[2]) * (current[0] + next[0]);
            n[2] += (current[0] - next[0]) * (current[1] + next[1]);
        }
        const double len = length3(n);
        Plane plane;
        if (len < 1e-300) {
            plane.normal = Vec3{0.0, 0.0, 1.0};
        } else {
            plane.normal = mul3(n, 1.0 / len);
        }
        plane.w = dot3(plane.normal, vertices[0]);
        return plane;
    }

    double distanceTo(const Vec3& vertex) const {
        return dot3(normal, vertex) - w;
    }

    void flip() {
        normal = mul3(normal, -1.0);
        w = -w;
    }
};

struct Polygon {
    std::vector<Vec3> vertices;
    Plane plane;

    void resetPlane() { plane = Plane::fromVertices(vertices); }

    void flip() {
        for (size_t i = 0, j = vertices.size() - 1; i < j; ++i, --j) {
            std::swap(vertices[i], vertices[j]);
        }
        plane.flip();
    }
};

std::vector<Polygon> toPolygons(const std::vector<Triangle>& triangles) {
    std::vector<Polygon> polygons;
    polygons.reserve(triangles.size());
    for (const Triangle& tri : triangles) {
        Polygon polygon;
        polygon.vertices.assign(tri.begin(), tri.end());
        polygon.resetPlane();
        polygons.push_back(std::move(polygon));
    }
    return polygons;
}

Mesh toMesh(const std::vector<Polygon>& polygons) {
    Mesh mesh;
    for (const Polygon& polygon : polygons) {
        if (polygon.vertices.size() < 3) {
            continue;
        }
        // Fan-triangulate; split outputs may carry more than 3 vertices.
        for (size_t i = 1; i + 1 < polygon.vertices.size(); ++i) {
            mesh.addTriangle(Triangle{polygon.vertices[0], polygon.vertices[i],
                                      polygon.vertices[i + 1]});
        }
    }
    return mesh;
}

// csg.js splitPolygon, ported faithfully: vertices are classified three-way
// (front / back / coincident within +-eps). Coincident vertices feed BOTH
// halves, and crossings interpolate strictly between a front and a back
// vertex — that discipline keeps splits total and terminates the BSP build.
void splitPolygon(const Polygon& polygon, const Plane& plane,
                  std::vector<Polygon>* front, std::vector<Polygon>* back,
                  std::vector<Polygon>* coplanarFront,
                  std::vector<Polygon>* coplanarBack) {
    const size_t count = polygon.vertices.size();
    if (count < 3) {
        return;
    }
    std::vector<VtType> types(count);
    int numFront = 0;
    int numBack = 0;
    int numCoincident = 0;
    for (size_t i = 0; i < count; ++i) {
        const double d = plane.distanceTo(polygon.vertices[i]);
        if (d > kEpsilon) {
            types[i] = VtType::Front;
            ++numFront;
        } else if (d < -kEpsilon) {
            types[i] = VtType::Back;
            ++numBack;
        } else {
            types[i] = VtType::Coincident;
            ++numCoincident;
        }
    }

    if (numCoincident == static_cast<int>(count)) {
        (dot3(plane.normal, polygon.plane.normal) > 0.0 ? coplanarFront
                                                        : coplanarBack)
            ->push_back(polygon);
        return;
    }
    if (numFront == 0 && numBack == 0) {
        (dot3(plane.normal, polygon.plane.normal) > 0.0 ? coplanarFront
                                                        : coplanarBack)
            ->push_back(polygon);
        return;
    }
    if (numBack == 0) {
        front->push_back(polygon);
        return;
    }
    if (numFront == 0) {
        back->push_back(polygon);
        return;
    }

    // Spanning: walk the loop, feeding each vertex to its side(s) and
    // interpolating a shared crossing point wherever the winding flips.
    std::vector<Vec3> f;
    std::vector<Vec3> b;
    f.reserve(count + 2);
    b.reserve(count + 2);
    auto dotVertex = [&plane](const Vec3& v) {
        return plane.distanceTo(v);
    };
    for (size_t i = 0; i < count; ++i) {
        const size_t j = (i + 1) % count;
        const Vec3& vi = polygon.vertices[i];
        const Vec3& vj = polygon.vertices[j];
        const VtType ti = types[i];
        const VtType tj = types[j];
        if (ti == VtType::Coincident) {
            f.push_back(vi);
            b.push_back(vi);
        } else if (ti == VtType::Front) {
            f.push_back(vi);
        } else {
            b.push_back(vi);
        }
        const bool crossing =
            (ti == VtType::Front && tj == VtType::Back) ||
            (ti == VtType::Back && tj == VtType::Front);
        if (crossing) {
            const double di = dotVertex(vi);
            const double dj = dotVertex(vj);
            // Opposite strict signs guarantee t in (0, 1); clamping the
            // boundary keeps floating noise from pushing t out to the ends.
            double t = di / (di - dj);
            if (!(t >= 0.0)) {
                t = 0.0;
            } else if (t > 1.0) {
                t = 1.0;
            }
            const Vec3 crossingPoint = add3(vi, mul3(sub3(vj, vi), t));
            f.push_back(crossingPoint);
            b.push_back(crossingPoint);
        }
    }
    if (f.size() >= 3) {
        Polygon splitF;
        splitF.vertices = std::move(f);
        splitF.resetPlane();
        front->push_back(std::move(splitF));
    }
    if (b.size() >= 3) {
        Polygon splitB;
        splitB.vertices = std::move(b);
        splitB.resetPlane();
        back->push_back(std::move(splitB));
    }
}

// BSP node, ported from CSG.Node. The boolean sequence below mirrors
// csg.js exactly, including clipPolygons' handling of missing children
// (keep the front half when no front child exists, drop the back half when
// no back child exists — invert() makes that correct by construction).
struct BspNode {
    bool hasPlane = false;
    Plane plane;
    std::vector<Polygon> polygons;
    std::unique_ptr<BspNode> front;
    std::unique_ptr<BspNode> back;

    BspNode() = default;

    explicit BspNode(const std::vector<Polygon>& initial) { build(initial); }

    void invert() {
        for (Polygon& polygon : polygons) {
            polygon.flip();
        }
        plane.flip();
        std::swap(front, back);
        if (front) {
            front->invert();
        }
        if (back) {
            back->invert();
        }
    }

    std::vector<Polygon> clipPolygons(const std::vector<Polygon>& polys) const {
        if (!hasPlane) {
            return polys;
        }
        std::vector<Polygon> frontList;
        std::vector<Polygon> backList;
        frontList.reserve(polys.size());
        for (const Polygon& polygon : polys) {
            splitPolygon(polygon, plane, &frontList, &backList, &frontList,
                         &backList);
        }
        if (front) {
            frontList = front->clipPolygons(frontList);
        }
        if (back) {
            backList = back->clipPolygons(backList);
        } else {
            backList.clear();
        }
        frontList.insert(frontList.end(),
                         std::make_move_iterator(backList.begin()),
                         std::make_move_iterator(backList.end()));
        return frontList;
    }

    void clipTo(const BspNode* other) {
        polygons = other->clipPolygons(polygons);
        if (front) {
            front->clipTo(other);
        }
        if (back) {
            back->clipTo(other);
        }
    }

    void collect(std::vector<Polygon>& out) const {
        out.insert(out.end(), polygons.begin(), polygons.end());
        if (front) {
            front->collect(out);
        }
        if (back) {
            back->collect(out);
        }
    }

    std::vector<Polygon> allPolygons() const {
        std::vector<Polygon> out;
        collect(out);
        return out;
    }

    void build(const std::vector<Polygon>& initial) {
        if (initial.empty()) {
            return;
        }
        if (!hasPlane) {
            plane = initial.front().plane;
            hasPlane = true;
        }
        std::vector<Polygon> frontPolys;
        std::vector<Polygon> backPolys;
        // csg.js build: coplanar polygons (both orientations) stay in this
        // node; strictly-front/back pieces recurse into child nodes.
        // Argument order matters: front, back, coplanarFront, coplanarBack.
        for (const Polygon& polygon : initial) {
            splitPolygon(polygon, plane, &frontPolys, &backPolys, &polygons,
                         &polygons);
        }
        if (!frontPolys.empty()) {
            if (!front) {
                front = std::make_unique<BspNode>();
            }
            front->build(frontPolys);
        }
        if (!backPolys.empty()) {
            if (!back) {
                back = std::make_unique<BspNode>();
            }
            back->build(backPolys);
        }
    }
};

Mesh csgApply(const Mesh& a, const Mesh& b, CsgOperation op) {
    const size_t aCount = a.triangleCount();
    const size_t bCount = b.triangleCount();
    if (aCount == 0 && bCount == 0) {
        return Mesh();
    }
    switch (op) {
        case CsgOperation::Union:
            if (aCount == 0) {
                return b;
            }
            if (bCount == 0) {
                return a;
            }
            break;
        case CsgOperation::Subtract:
            if (bCount == 0) {
                return a;
            }
            if (aCount == 0) {
                return Mesh();
            }
            break;
        case CsgOperation::Intersect:
            if (aCount == 0 || bCount == 0) {
                return Mesh();
            }
            break;
    }

    BspNode aNode(toPolygons(a.triangles()));
    BspNode bNode(toPolygons(b.triangles()));
    // Flows are verbatim csg.js (union/subtract from the reference header
    // comment, intersect via A & B = ~(~A | ~B)).
    switch (op) {
        case CsgOperation::Union:
            aNode.clipTo(&bNode);
            bNode.clipTo(&aNode);
            bNode.invert();
            bNode.clipTo(&aNode);
            bNode.invert();
            aNode.build(bNode.allPolygons());
            return toMesh(aNode.allPolygons());
        case CsgOperation::Subtract:
            aNode.invert();
            aNode.clipTo(&bNode);
            bNode.clipTo(&aNode);
            bNode.invert();
            bNode.clipTo(&aNode);
            bNode.invert();
            aNode.build(bNode.allPolygons());
            aNode.invert();
            return toMesh(aNode.allPolygons());
        case CsgOperation::Intersect:
            aNode.invert();
            bNode.clipTo(&aNode);
            bNode.invert();
            aNode.clipTo(&bNode);
            bNode.clipTo(&aNode);
            aNode.build(bNode.allPolygons());
            aNode.invert();
            return toMesh(aNode.allPolygons());
    }
    return Mesh();
}

std::size_t hashEdge(const Vec3& p, const Vec3& q) {
    // FNV-1a over the quantized endpoint coordinates (exact integers stored
    // in doubles, so identical points have identical bit patterns).
    std::size_t hash = 14695981039346656037ull;
    const double values[6] = {p[0], p[1], p[2], q[0], q[1], q[2]};
    for (double value : values) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int shift = 0; shift < 64; shift += 8) {
            hash = (hash ^ static_cast<std::size_t>((bits >> shift) & 0xff)) *
                   1099511628211ull;
        }
    }
    return hash;
}

// T-junction refinement over a triangle soup. BSP clipping only inserts
// split vertices along polygon boundaries, so an operand's internal
// triangulation diagonal can cross a clip plane at a point that does not
// exist along the neighboring surface whose edges span the same line (the
// reference csg.js avoids this by keeping whole n-gon faces; triangle-soup
// operands cannot avoid it without pre-merging). This pass subdivides every
// triangle edge that contains intermediate mesh vertices, restoring strict
// two-triangle edge sharing. It is pure subdivision: the enclosed volume is
// unchanged and orientation is preserved by fanning around the opposite
// corner.
std::vector<Triangle> refineTJunctions(const std::vector<Triangle>& input) {
    // Tolerance: vertex coordinates quantize to a 1e-6 mm grid; a point
    // counts as lying on an edge when its perpendicular distance and its
    // distance to the edge endpoints stay within one grid step.
    constexpr double kOnEdgeTol = 1e-6;

    auto keyOf = [](const Vec3& v) {
        std::array<long long, 3> key{};
        for (int axis = 0; axis < 3; ++axis) {
            key[axis] = std::llround(v[axis] * 1e6);
        }
        return key;
    };

    // Vertex pool: deduped representatives of every vertex in the mesh.
    std::map<std::array<long long, 3>, Vec3> pool;
    for (const Triangle& tri : input) {
        for (int i = 0; i < 3; ++i) {
            pool.emplace(keyOf(tri[i]), tri[i]);
        }
    }

    std::vector<Triangle> current = input;
    // Subdividing one edge can expose intermediate vertices on the fan's
    // other edges; iterate to a fixed point (bounded: each pass adds at
    // least one pairing, meshes here are small).
    for (int pass = 0; pass < 8; ++pass) {
        std::vector<Triangle> next;
        next.reserve(current.size());
        bool changed = false;
        for (const Triangle& tri : current) {
            const std::array<Vec3, 3> pts = {tri[0], tri[1], tri[2]};
            bool split = false;
            for (int edge = 0; edge < 3 && !split; ++edge) {
                const Vec3& a = pts[edge];
                const Vec3& b = pts[(edge + 1) % 3];
                const Vec3& c = pts[(edge + 2) % 3];
                const auto keyA = keyOf(a);
                const auto keyB = keyOf(b);
                const Vec3 ab = sub3(b, a);
                const double len2 = dot3(ab, ab);
                if (len2 < 1e-20) {
                    continue;
                }
                std::vector<std::pair<double, Vec3>> cuts;
                for (const auto& entry : pool) {
                    // Same-grid-cell points are the endpoints themselves (or
                    // their float-noise twins); skip them so no degenerate
                    // same-cell edge can be created.
                    if (entry.first == keyA || entry.first == keyB) {
                        continue;
                    }
                    const Vec3& p = entry.second;
                    const double t =
                        dot3(sub3(p, a), ab) / len2;
                    if (!(t > 0.0) || !(t < 1.0)) {
                        continue;
                    }
                    if (length3(sub3(p, add3(a, mul3(ab, t)))) >
                        kOnEdgeTol) {
                        continue;
                    }
                    cuts.emplace_back(t, p);
                }
                if (cuts.empty()) {
                    continue;
                }
                std::sort(cuts.begin(), cuts.end(),
                          [](const std::pair<double, Vec3>& x,
                             const std::pair<double, Vec3>& y) {
                              return x.first < y.first;
                          });
                Vec3 prev = a;
                for (const auto& item : cuts) {
                    next.push_back(Triangle{prev, item.second, c});
                    prev = item.second;
                }
                next.push_back(Triangle{prev, b, c});
                changed = true;
                split = true;
            }
            if (!split) {
                next.push_back(tri);
            }
        }
        current = std::move(next);
        if (!changed) {
            break;
        }
    }
    return current;
}

}  // namespace

Mesh booleanMesh(const Mesh& a, const Mesh& b, CsgOperation op) {
    const Mesh raw = csgApply(a, b, op);
    // Identity results (empty operands, disjoint intersection) are returned
    // untouched; only genuinely combined geometry needs seam healing.
    if (raw.empty() || a.triangleCount() == 0 || b.triangleCount() == 0) {
        return raw;
    }
    Mesh healed;
    healed.reserveTriangles(raw.triangleCount());
    for (const Triangle& tri : refineTJunctions(raw.triangles())) {
        healed.addTriangle(tri);
    }
    return healed;
}

Mesh csgUnion(const Mesh& a, const Mesh& b) {
    return booleanMesh(a, b, CsgOperation::Union);
}

Mesh csgIntersect(const Mesh& a, const Mesh& b) {
    return booleanMesh(a, b, CsgOperation::Intersect);
}

Mesh csgSubtract(const Mesh& a, const Mesh& b) {
    return booleanMesh(a, b, CsgOperation::Subtract);
}

double signedVolume(const Mesh& mesh) {
    double total = 0.0;
    for (const Triangle& tri : mesh.triangles()) {
        // Signed tetrahedron volume (origin, v0, v1, v2): the divergence
        // theorem accumulated over the triangle soup.
        total += dot3(crossProduct(tri[0], tri[1]), tri[2]) / 6.0;
    }
    return total;
}

bool isWatertight(const Mesh& mesh) {
    // Every undirected edge must be shared by exactly two triangles.
    // Endpoints are quantized to a 1e-6 mm grid so points produced by the
    // same split compare equal.
    if (mesh.empty()) {
        return false;
    }
    std::unordered_map<std::size_t, int> edgeUse;
    edgeUse.reserve(mesh.triangleCount() * 3);
    for (const Triangle& tri : mesh.triangles()) {
        std::array<Vec3, 3> quantized{};
        for (size_t i = 0; i < 3; ++i) {
            for (size_t axis = 0; axis < 3; ++axis) {
                quantized[i][axis] =
                    std::llround(tri[i][axis] * 1e6);
            }
        }
        for (size_t i = 0; i < 3; ++i) {
            const Vec3& p = quantized[i];
            const Vec3& q = quantized[(i + 1) % 3];
            // Exact integer equality after quantization; equal endpoints
            // mean a degenerate edge and the whole triangle is suspect.
            bool degenerate = p[0] == q[0] && p[1] == q[1] && p[2] == q[2];
            if (degenerate) {
                return false;
            }
            const bool pFirst =
                p[0] < q[0] || (p[0] == q[0] && (p[1] < q[1] ||
                                                (p[1] == q[1] &&
                                                 p[2] < q[2])));
            ++edgeUse[hashEdge(pFirst ? p : q, pFirst ? q : p)];
        }
    }
    for (const auto& entry : edgeUse) {
        if (entry.second != 2) {
            return false;
        }
    }
    return true;
}

}  // namespace trinity::cad
