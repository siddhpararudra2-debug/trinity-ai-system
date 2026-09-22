#include "trinity/vision/VisionProject.hpp"

#include <stdexcept>

namespace trinity::vision {

std::string toString(VisionOperation op) {
    switch (op) {
        case VisionOperation::LoadImage: return "load_image";
        case VisionOperation::ResizeImage: return "resize_image";
        case VisionOperation::Grayscale: return "grayscale";
        case VisionOperation::EdgeDetect: return "edge_detect";
        case VisionOperation::ImageStatistics: return "image_statistics";
        case VisionOperation::Describe: return "describe";
        default: return "unknown";
    }
}

VisionOperation visionOperationFromString(const std::string& s) {
    if (s == "load_image") return VisionOperation::LoadImage;
    if (s == "resize_image") return VisionOperation::ResizeImage;
    if (s == "grayscale") return VisionOperation::Grayscale;
    if (s == "edge_detect") return VisionOperation::EdgeDetect;
    if (s == "image_statistics") return VisionOperation::ImageStatistics;
    if (s == "describe") return VisionOperation::Describe;
    return VisionOperation::Unknown;
}

core::Json BoundingBox::toJson() const {
    return {{"x", x}, {"y", y}, {"width", width}, {"height", height}};
}

BoundingBox BoundingBox::fromJson(const core::Json& j) {
    BoundingBox b;
    b.x = j.value("x", 0);
    b.y = j.value("y", 0);
    b.width = j.value("width", 0);
    b.height = j.value("height", 0);
    return b;
}

core::Json Keypoint::toJson() const {
    return {{"x", x}, {"y", y}, {"score", score}};
}

Keypoint Keypoint::fromJson(const core::Json& j) {
    Keypoint k;
    k.x = j.value("x", 0.0);
    k.y = j.value("y", 0.0);
    k.score = j.value("score", 0.0);
    return k;
}

core::Json Detection::toJson() const {
    return {{"label", label}, {"confidence", confidence}, {"box", box.toJson()}};
}

Detection Detection::fromJson(const core::Json& j) {
    Detection d;
    d.label = j.value("label", "");
    d.confidence = j.value("confidence", 0.0);
    if (j.contains("box") && j["box"].is_object()) {
        d.box = BoundingBox::fromJson(j["box"]);
    }
    return d;
}

core::Json ImageMetadata::toJson() const {
    return {{"width", width},
            {"height", height},
            {"channels", channels},
            {"format", format},
            {"size_bytes", sizeBytes}};
}

ImageMetadata ImageMetadata::fromJson(const core::Json& j) {
    ImageMetadata m;
    m.width = j.value("width", 0);
    m.height = j.value("height", 0);
    m.channels = j.value("channels", 0);
    m.format = j.value("format", "");
    m.sizeBytes = j.value("size_bytes", 0LL);
    return m;
}

core::Json ImageInput::toJson() const {
    return {{"path", path},
            {"artifact_id", artifactId},
            {"bytes_ref", bytesRef},
            {"metadata", metadata.toJson()}};
}

ImageInput ImageInput::fromJson(const core::Json& j) {
    ImageInput in;
    in.path = j.value("path", "");
    in.artifactId = j.value("artifact_id", "");
    in.bytesRef = j.value("bytes_ref", "");
    if (j.contains("metadata") && j["metadata"].is_object()) {
        in.metadata = ImageMetadata::fromJson(j["metadata"]);
    }
    return in;
}

core::Json ImageResult::toJson() const {
    return {{"success", success},
            {"operation", operation},
            {"input", input.toJson()},
            {"output", output.toJson()},
            {"statistics", statistics},
            {"artifact_paths", artifactPaths},
            {"message", message}};
}

ImageResult ImageResult::fromJson(const core::Json& j) {
    ImageResult r;
    r.success = j.value("success", false);
    r.operation = j.value("operation", "");
    if (j.contains("input") && j["input"].is_object()) {
        r.input = ImageInput::fromJson(j["input"]);
    }
    if (j.contains("output") && j["output"].is_object()) {
        r.output = ImageInput::fromJson(j["output"]);
    }
    if (j.contains("statistics") && j["statistics"].is_object()) {
        r.statistics = j["statistics"];
    }
    if (j.contains("artifact_paths") && j["artifact_paths"].is_array()) {
        for (const auto& p : j["artifact_paths"]) {
            if (p.is_string()) {
                r.artifactPaths.push_back(p.get<std::string>());
            }
        }
    }
    r.message = j.value("message", "");
    return r;
}

core::Json VisionProject::toJson() const {
    core::Json out = core::Json::object();
    out["name"] = name;
    out["source"] = source.toJson();
    out["last_metadata"] = lastMetadata.toJson();
    
    core::Json dets = core::Json::array();
    for (const auto& d : detections) {
        dets.push_back(d.toJson());
    }
    out["detections"] = dets;

    core::Json kpts = core::Json::array();
    for (const auto& k : keypoints) {
        kpts.push_back(k.toJson());
    }
    out["keypoints"] = kpts;

    out["processing"] = processing;
    out["metadata"] = metadata;
    return out;
}

VisionProject VisionProject::fromJson(const core::Json& j) {
    VisionProject p;
    p.name = j.value("name", "vision-project");
    if (j.contains("source") && j["source"].is_object()) {
        p.source = ImageInput::fromJson(j["source"]);
    }
    if (j.contains("last_metadata") && j["last_metadata"].is_object()) {
        p.lastMetadata = ImageMetadata::fromJson(j["last_metadata"]);
    }
    if (j.contains("detections") && j["detections"].is_array()) {
        for (const auto& d : j["detections"]) {
            p.detections.push_back(Detection::fromJson(d));
        }
    }
    if (j.contains("keypoints") && j["keypoints"].is_array()) {
        for (const auto& k : j["keypoints"]) {
            p.keypoints.push_back(Keypoint::fromJson(k));
        }
    }
    if (j.contains("processing") && j["processing"].is_object()) {
        p.processing = j["processing"];
    }
    if (j.contains("metadata") && j["metadata"].is_object()) {
        p.metadata = j["metadata"];
    }
    return p;
}

}  // namespace trinity::vision
