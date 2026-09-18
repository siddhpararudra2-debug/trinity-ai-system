#include "ScaffoldEngines.hpp"

namespace trinity::engines {

void register_scaffold_engines(std::vector<std::string>& registered) {
    const std::pair<const char*, std::vector<std::string>> scaffolds[] = {
        {"pcb", {"inspect", "validate", "generate", "export"}},
        {"firmware", {"create", "build", "test", "compile"}},
        {"vision", {"image_inspect", "ocr", "document_parse", "geometry_extract"}},
        {"research", {"search", "query", "cite"}},
        {"simulation", {"simulate", "configure", "results"}},
        {"robotics", {"kinematics", "trajectory_plan"}},
    };
    for (const auto& [id, capabilities] : scaffolds) {
        auto engine = std::make_shared<ScaffoldEngine>(id, id, capabilities);
        if (EngineRegistry::instance().register_engine(engine).is_ok()) {
            registered.push_back(id);
        }
    }
}

}  // namespace trinity::engines
