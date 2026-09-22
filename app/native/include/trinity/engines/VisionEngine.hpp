#pragma once

// Real VisionEngine: deterministic load/resize/grayscale/Canny/stats
// over the tool-agnostic Vision IR. OpenCV is confined to
// ImageProcessor; this engine only exchanges JSON + scratch paths.
// State (ImageInput) chains between operations so jobs and workflow
// nodes flow: load -> resize -> grayscale -> edge_detect / statistics.

#include <string>
#include <vector>

#include "Engine.hpp"

namespace trinity::engines {

class VisionEngine : public EngineBase {
public:
    VisionEngine();

    EngineResult execute(const EngineRequest& request) override;
    validation::ValidationResult validate(const EngineResult& result) const override;

    /// Supported formats for the UI combo / error messages.
    static std::vector<std::string> supportedFormats();

private:
    EngineResult executeLoadImage(const EngineRequest& request);
    EngineResult executeResizeImage(const EngineRequest& request);
    EngineResult executeGrayscale(const EngineRequest& request);
    EngineResult executeEdgeDetect(const EngineRequest& request);
    EngineResult executeImageStatistics(const EngineRequest& request);

    // Resolve `path` or `artifact_id` parameter into a concrete local
    // file path. Throws RequestValidationError when neither is present
    // or the file does not exist — never invents a path.
    static std::string resolveImagePath(const core::Json& params);
};

}  // namespace trinity::engines
