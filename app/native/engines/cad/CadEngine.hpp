// Trinity — CAD engine orchestration and the ICADAdapter boundary.
//
// Adapters (brief §CAD): Onshape, CadQuery, FreeCAD, OpenSCAD are optional
// backends behind one interface. V1 ships the dependency-free MeshKernel
// adapter only; the others are honest stubs reporting capability_unavailable
// exactly like the Python adapters. The engine never assumes one provider.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Mesh.hpp"
#include "QuadcopterFrame.hpp"
#include "../Engine.hpp"

namespace trinity::engines::cad {

class ICADAdapter {
public:
    virtual ~ICADAdapter() = default;

    virtual std::string name() const = 0;

    struct GenerateResult {
        bool ok = false;
        Mesh mesh;
        std::string error;
    };

    virtual GenerateResult generate(const QuadcopterFrameIR& ir) = 0;
    virtual bool import_model(const std::string& path, Mesh& out_mesh) = 0;
    virtual bool export_model(const Mesh& mesh, const std::string& format,
                              const std::string& output_path) = 0;
    virtual core::Json validate(const QuadcopterFrameIR& ir, const Mesh& mesh) = 0;
    virtual core::Json preview(const Mesh& mesh) = 0;
};

// Local fallback adapter — no external kernel, box geometry, binary STL.
class MeshKernelAdapter : public ICADAdapter {
public:
    std::string name() const override { return "mesh_kernel"; }
    GenerateResult generate(const QuadcopterFrameIR& ir) override;
    bool import_model(const std::string& path, Mesh& out_mesh) override;
    bool export_model(const Mesh& mesh, const std::string& format,
                      const std::string& output_path) override;
    core::Json validate(const QuadcopterFrameIR& ir, const Mesh& mesh) override;
    core::Json preview(const Mesh& mesh) override;
};

// Placeholder adapter that honestly reports an unavailable external kernel.
class UnavailableKernelAdapter : public ICADAdapter {
public:
    explicit UnavailableKernelAdapter(std::string name, std::string kernel_hint)
        : name_(std::move(name)), kernel_hint_(std::move(kernel_hint)) {}

    std::string name() const override { return name_; }
    GenerateResult generate(const QuadcopterFrameIR& ir) override;
    bool import_model(const std::string& path, Mesh& out_mesh) override;
    bool export_model(const Mesh& mesh, const std::string& format,
                      const std::string& output_path) override;
    core::Json validate(const QuadcopterFrameIR& ir, const Mesh& mesh) override;
    core::Json preview(const Mesh& mesh) override;

private:
    std::string name_;
    std::string kernel_hint_;
};

// ---------------------------------------------------------------- engine

class CadEngine : public IEngine {
public:
    CadEngine();

    EngineDescriptor describe() const override;
    ExecutionOutput execute(const std::string& capability,
                            const core::Json& parameters) override;
    core::Json validate(const core::Json& payload) override;
    EngineHealth health() const override;

    // Adapter registration (the mesh kernel is registered at construction).
    void register_adapter(std::shared_ptr<ICADAdapter> adapter);
    std::vector<std::string> adapter_names() const;

private:
    std::shared_ptr<ICADAdapter> active_adapter_;
    std::vector<std::shared_ptr<ICADAdapter>> adapters_;
};

}  // namespace trinity::engines::cad
