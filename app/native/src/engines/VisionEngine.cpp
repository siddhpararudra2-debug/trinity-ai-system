#include "trinity/engines/VisionEngine.hpp"

#include <filesystem>
#include <iostream>

#include "trinity/core/Error.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/vision/ImageProcessor.hpp"

namespace trinity::engines {

VisionEngine::VisionEngine() {
    name_ = "vision";
    version_ = "1.0";
    capabilities_ = {
        "describe",
        "load_image",
        "resize_image",
        "grayscale",
        "edge_detect",
        "image_statistics",
        "extract_contours",
        "match_features"
    };
}

std::vector<std::string> VisionEngine::supportedFormats() {
    return {"png", "jpg", "jpeg", "bmp"};
}

std::string VisionEngine::resolveImagePath(const core::Json& params) {
    if (params.contains("path") && params["path"].is_string()) {
        std::string path = params["path"].get<std::string>();
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }
    
    // If we only have artifact_id but no path, the JobWorker should have
    // resolved it before calling the engine, or the frontend should pass both.
    // If it's missing, we fail cleanly.
    if (params.contains("artifact_id") && params["artifact_id"].is_string()) {
        throw core::RequestValidationError(
            "VisionEngine requires a concrete 'path' for artifact_id",
            {{"artifact_id", params["artifact_id"]}});
    }

    throw core::RequestValidationError(
        "Missing or invalid 'path' parameter for vision operation");
}

EngineResult VisionEngine::execute(const EngineRequest& request) {
    if (request.operation == "describe") {
        EngineResult out = successResult(
            request, {{"engine", name_},
                      {"version", version_},
                      {"capabilities", capabilities_},
                      {"supported_formats", supportedFormats()}});
        out.validation = validate(out);
        return out;
    }
    requireCapability(request);

    try {
        if (request.operation == "load_image") {
            return executeLoadImage(request);
        } else if (request.operation == "resize_image") {
            return executeResizeImage(request);
        } else if (request.operation == "grayscale") {
            return executeGrayscale(request);
        } else if (request.operation == "edge_detect") {
            return executeEdgeDetect(request);
        } else if (request.operation == "image_statistics") {
            return executeImageStatistics(request);
        } else if (request.operation == "extract_contours") {
            return executeExtractContours(request);
        } else if (request.operation == "match_features") {
            return executeMatchFeatures(request);
        }
    } catch (const core::RequestValidationError& e) {
        return invalidRequest(e.what(), e.details());
    } catch (const std::exception& e) {
        return failureResult(request, e.what());
    }

    return capabilityUnavailable(request);
}

validation::ValidationResult VisionEngine::validate(const EngineResult& result) const {
    validation::ValidationResult v;
    v.operation = result.operation;
    v.checks = {{"engine", name_}, {"operation", result.operation}};

    if (result.failed()) {
        v.status = validation::ValidationStatus::Invalid;
        v.message = "Vision operation failed";
        return v;
    }

    if (result.result.contains("output") && result.result["output"].is_object()) {
        v.status = validation::ValidationStatus::Generated;
        v.message = "Output image generated successfully";
    } else {
        v.status = validation::ValidationStatus::Validated;
        v.message = "Vision operation completed successfully";
    }

    return v;
}

EngineResult VisionEngine::executeLoadImage(const EngineRequest& request) {
    std::string path = resolveImagePath(request.parameters);
    
    vision::ImageMetadata meta = vision::ImageProcessor::loadImage(path);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = meta;
    
    return successResult(request, res.toJson());
}

EngineResult VisionEngine::executeResizeImage(const EngineRequest& request) {
    requireParams(request, {"width", "height"});
    std::string path = resolveImagePath(request.parameters);
    
    int width = request.parameters["width"].get<int>();
    int height = request.parameters["height"].get<int>();
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    
    std::string outName = "resized_" + core::newUuid() + "." + inMeta.format;
    std::string outPath = (std::filesystem::path(std::filesystem::temp_directory_path()) / outName).string();
    
    vision::ImageMetadata outMeta = vision::ImageProcessor::resizeImage(path, width, height, outPath);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    res.output.path = outPath;
    res.output.metadata = outMeta;
    res.artifactPaths.push_back(outPath);
    
    EngineResult result = successResult(request, res.toJson());
    result.pendingArtifacts.push_back({outPath, "Image"});
    return result;
}

EngineResult VisionEngine::executeGrayscale(const EngineRequest& request) {
    std::string path = resolveImagePath(request.parameters);
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    
    std::string outName = "grayscale_" + core::newUuid() + "." + inMeta.format;
    std::string outPath = (std::filesystem::path(std::filesystem::temp_directory_path()) / outName).string();
    
    vision::ImageMetadata outMeta = vision::ImageProcessor::grayscale(path, outPath);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    res.output.path = outPath;
    res.output.metadata = outMeta;
    res.artifactPaths.push_back(outPath);
    
    EngineResult result = successResult(request, res.toJson());
    result.pendingArtifacts.push_back({outPath, "Image"});
    return result;
}

EngineResult VisionEngine::executeEdgeDetect(const EngineRequest& request) {
    std::string path = resolveImagePath(request.parameters);
    
    double low = request.parameters.value("low", 50.0);
    double high = request.parameters.value("high", 150.0);
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    
    // Canny output is best saved as PNG to avoid compression artifacts.
    std::string outName = "edges_" + core::newUuid() + ".png";
    std::string outPath = (std::filesystem::path(std::filesystem::temp_directory_path()) / outName).string();
    
    vision::ImageMetadata outMeta = vision::ImageProcessor::edgeDetect(path, low, high, outPath);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    res.output.path = outPath;
    res.output.metadata = outMeta;
    res.artifactPaths.push_back(outPath);
    
    EngineResult result = successResult(request, res.toJson());
    result.pendingArtifacts.push_back({outPath, "Image"});
    return result;
}

EngineResult VisionEngine::executeImageStatistics(const EngineRequest& request) {
    std::string path = resolveImagePath(request.parameters);
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    vision::ProcessedStats stats = vision::ImageProcessor::imageStatistics(path);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    res.statistics = stats.toJson();
    
    return successResult(request, res.toJson());
}

EngineResult VisionEngine::executeExtractContours(const EngineRequest& request) {
    std::string path = resolveImagePath(request.parameters);
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    const double cannyLow = request.parameters.value("canny_low", 50.0);
    const double cannyHigh = request.parameters.value("canny_high", 150.0);
    const double minArea = request.parameters.value("min_area_px2", 100.0);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    res.statistics = vision::ImageProcessor::extractContours(path, cannyLow,
                                                             cannyHigh, minArea)
                         .toJson();
    
    return successResult(request, res.toJson());
}

EngineResult VisionEngine::executeMatchFeatures(const EngineRequest& request) {
    if (!request.parameters.contains("second_path") ||
        !request.parameters["second_path"].is_string()) {
        throw core::RequestValidationError(
            "match_features requires 'second_path' (the image to match against)",
            {}, "engines");
    }
    std::string path = resolveImagePath(request.parameters);
    const std::string second = request.parameters["second_path"].get<std::string>();
    const double ratio = request.parameters.value("distance_ratio", 0.75);
    
    vision::ImageMetadata inMeta = vision::ImageProcessor::loadImage(path);
    vision::ImageMetadata bMeta = vision::ImageProcessor::loadImage(second);
    vision::FeatureMatchResult matches =
        vision::ImageProcessor::matchFeatures(path, second, ratio);
    
    vision::ImageResult res;
    res.success = true;
    res.operation = request.operation;
    res.input.path = path;
    res.input.metadata = inMeta;
    // Second-image metadata piggybacks on the statistics field so
    // ImageResult keeps its shape.
    res.statistics = {{"first", inMeta.toJson()},
                      {"second", bMeta.toJson()},
                      {"keypoints_a", matches.keypointsA},
                      {"keypoints_b", matches.keypointsB},
                      {"match_count", matches.matchCount},
                      {"match_distances", [&matches] {
                           core::Json dists = core::Json::array();
                           for (double d : matches.matchDistances) {
                               dists.push_back(d);
                           }
                           return dists;
                       }()}};
    
    return successResult(request, res.toJson());
}

}  // namespace trinity::engines
