#include "GeometryLoader.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>
#include <cmath>

#include "../../core/Logging.hpp"

namespace trinity::rendering {
namespace {
core::ComponentLog log_("geom");
std::string lower_ext(const std::string& p) {
    auto pos = p.rfind('.');
    if (pos == std::string::npos) return "";
    std::string e = p.substr(pos+1);
    for (auto& c : e) c = static_cast<char>(::tolower(c));
    return e;
}
void fill_buffers(LoadedGeometry& out) {
    out.positions.clear(); out.normals.clear(); out.indices.clear();
    out.positions.reserve(out.mesh.triangles.size()*9);
    out.normals.reserve(out.mesh.triangles.size()*9);
    out.indices.reserve(out.mesh.triangles.size()*3);
    uint32_t idx = 0;
    for (auto& tri : out.mesh.triangles) {
        auto& [v0,v1,v2] = tri;
        auto [x0,y0,z0] = v0; auto [x1,y1,z1] = v1; auto [x2,y2,z2] = v2;
        // positions
        out.positions.push_back((float)x0); out.positions.push_back((float)y0); out.positions.push_back((float)z0);
        out.positions.push_back((float)x1); out.positions.push_back((float)y1); out.positions.push_back((float)z1);
        out.positions.push_back((float)x2); out.positions.push_back((float)y2); out.positions.push_back((float)z2);
        // normals (per-triangle)
        double ux = x1-x0, uy = y1-y0, uz = z1-z0;
        double vx = x2-x0, vy = y2-y0, vz = z2-z0;
        double nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
        double len = std::sqrt(nx*nx+ny*ny+nz*nz);
        if (len < 1e-12) { nx=0; ny=0; nz=1; } else { nx/=len; ny/=len; nz/=len; }
        for (int i=0;i<3;i++) { out.normals.push_back((float)nx); out.normals.push_back((float)ny); out.normals.push_back((float)nz); }
        out.indices.push_back(idx++); out.indices.push_back(idx++); out.indices.push_back(idx++);
    }
    auto bb = out.mesh.bounding_box();
    out.bounding_mm[0]=std::get<0>(bb.first); out.bounding_mm[1]=std::get<1>(bb.first); out.bounding_mm[2]=std::get<2>(bb.first);
    out.bounding_mm[3]=std::get<0>(bb.second); out.bounding_mm[4]=std::get<1>(bb.second); out.bounding_mm[5]=std::get<2>(bb.second);
    out.triangle_count = out.mesh.triangles.size();
}
} // namespace

LoadedGeometry GeometryLoader::from_mesh(const engines::cad::Mesh& mesh, const std::string& artifact_id) {
    LoadedGeometry out;
    out.ok = true;
    out.format = "mesh";
    out.mesh = mesh;
    out.artifact_id = artifact_id;
    fill_buffers(out);
    return out;
}

LoadedGeometry GeometryLoader::load_stl_binary(const std::string& file_path) {
    LoadedGeometry out;
    out.format = "stl";
    std::ifstream in(file_path, std::ios::binary);
    if (!in) { out.error = "cannot open " + file_path; return out; }
    char header[80] = {0};
    in.read(header,80);
    if (!in) { out.error = "short header"; return out; }
    uint32_t n = 0;
    in.read(reinterpret_cast<char*>(&n),4);
    if (!in) { out.error = "short count"; return out; }
    // STL is little-endian — matches host on x64.
    // Guard against huge files (1M tris ~ 50MB)
    if (n > 4000000) { out.error = "STL triangle count insane: " + std::to_string(n); return out; }
    out.mesh.triangles.reserve(n);
    for (uint32_t i=0;i<n;++i) {
        float nx,ny,nz;
        float x0,y0,z0,x1,y1,z1,x2,y2,z2;
        uint16_t attr;
        in.read(reinterpret_cast<char*>(&nx),4); in.read(reinterpret_cast<char*>(&ny),4); in.read(reinterpret_cast<char*>(&nz),4);
        in.read(reinterpret_cast<char*>(&x0),4); in.read(reinterpret_cast<char*>(&y0),4); in.read(reinterpret_cast<char*>(&z0),4);
        in.read(reinterpret_cast<char*>(&x1),4); in.read(reinterpret_cast<char*>(&y1),4); in.read(reinterpret_cast<char*>(&z1),4);
        in.read(reinterpret_cast<char*>(&x2),4); in.read(reinterpret_cast<char*>(&y2),4); in.read(reinterpret_cast<char*>(&z2),4);
        in.read(reinterpret_cast<char*>(&attr),2);
        if (!in) { out.error = "short triangle "+std::to_string(i); return out; }
        engines::cad::Triangle tri = {{x0,y0,z0},{x1,y1,z1},{x2,y2,z2}};
        out.mesh.triangles.push_back(tri);
        (void)nx;(void)ny;(void)nz;(void)attr;
    }
    out.ok = true;
    fill_buffers(out);
    return out;
}

LoadedGeometry GeometryLoader::load_glb(const std::string& file_path) {
    LoadedGeometry out;
    out.format = "glb";
    // Minimal: do not parse glTF JSON — native mesh is authoritative.
    // For now report scaffolded if GLB requested but no parser yet.
    // The native STL is the rendered source; GLB remains via Python host.
    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in) { out.error = "cannot open GLB " + file_path; return out; }
    size_t sz = (size_t)in.tellg();
    if (sz < 20) { out.error = "GLB too small"; return out; }
    in.seekg(0);
    char magic[4]={0};
    in.read(magic,4);
    if (std::string(magic,4)!="glTF") { out.error = "bad glTF magic"; return out; }
    uint32_t ver=0, len=0;
    in.read(reinterpret_cast<char*>(&ver),4); in.read(reinterpret_cast<char*>(&len),4);
    if (ver!=2) { out.error = "glTF version !=2"; return out; }
    out.error = "GLB parsing not yet implemented in native loader — render STL instead (honest scaffold)";
    log_.info("glb load scaffolded", [&]{ core::Json c=core::Json::object(); c["file"]=file_path; c["size"]=(double)sz; return c; }());
    return out;
}

LoadedGeometry GeometryLoader::load_obj(const std::string& file_path) {
    LoadedGeometry out;
    out.format="obj";
    out.error="OBJ not yet implemented — STL is the rendered path";
    (void)file_path;
    return out;
}

LoadedGeometry GeometryLoader::load(const std::string& file_path) {
    std::string e = lower_ext(file_path);
    if (e=="stl") return load_stl_binary(file_path);
    if (e=="glb") return load_glb(file_path);
    if (e=="obj") return load_obj(file_path);
    LoadedGeometry out;
    out.error="unsupported format: " + e;
    return out;
}

} // namespace trinity::rendering
