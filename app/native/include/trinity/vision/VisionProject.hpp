#pragma once

// Vision IR: tool-agnostic representation independent of OpenCV types.
// Mirrors FirmwareProject — engines and workflows exchange only these
// plain structs serialized as JSON; cv::Mat lives solely inside
// ImageProcessor.

#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::vision {

enum class VisionOperation {
    LoadImage,
    ResizeImage,
    Grayscale,
    EdgeDetect,
    ImageStatistics,
    Describe,
    Unknown,
};

std::string toString(VisionOperation op);
VisionOperation visionOperationFromString(const std::string& s);

struct BoundingBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    core::Json toJson() const;
    static BoundingBox fromJson(const core::Json& j);
};

struct Keypoint {
    double x = 0.0;
    double y = 0.0;
    double score = 0.0;

    core::Json toJson() const;
    static Keypoint fromJson(const core::Json& j);
};

struct Detection {
    std::string label;
    double confidence = 0.0;
    BoundingBox box;

    core::Json toJson() const;
    static Detection fromJson(const core::Json& j);
};

struct ImageMetadata {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string format;  // "png", "jpg", ...
    long long sizeBytes = 0;

    core::Json toJson() const;
    static ImageMetadata fromJson(const core::Json& j);
};

struct ImageInput {
    // Exactly one of path / artifactId / bytesRef is set. Bytes are
    // never embedded in JSON — only file paths and artifact ids flow.
    std::string path;
    std::string artifactId;
    std::string bytesRef;  // opaque handle, reserved for future use
    ImageMetadata metadata;

    bool empty() const noexcept {
        return path.empty() && artifactId.empty() && bytesRef.empty();
    }

    core::Json toJson() const;
    static ImageInput fromJson(const core::Json& j);
};

struct ImageResult {
    bool success = false;
    std::string operation;
    ImageInput input;
    ImageInput output;  // set when the op produced a new image file
    core::Json statistics = core::Json::object();  // mean/min/max arrays
    std::vector<std::string> artifactPaths;        // scratch paths for promotion
    std::string message;

    core::Json toJson() const;
    static ImageResult fromJson(const core::Json& j);
};

// Lightweight project wrapper for workflow chaining (optional but keeps
// the IR symmetric with FirmwareProject).
struct VisionProject {
    std::string name = "vision-project";
    ImageInput source;
    ImageMetadata lastMetadata;
    std::vector<Detection> detections;  // reserved; empty in V1
    std::vector<Keypoint> keypoints;    // reserved; empty in V1
    core::Json processing = core::Json::object();  // op history
    core::Json metadata = core::Json::object();

    core::Json toJson() const;
    static VisionProject fromJson(const core::Json& j);
};

}  // namespace trinity::vision
