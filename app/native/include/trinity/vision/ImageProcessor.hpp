#pragma once

// Image processing layer: the ONLY place OpenCV headers appear.
// Public API returns plain structs / filesystem paths so the rest of
// Trinity (engines, jobs, UI) never sees cv::Mat.

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "VisionProject.hpp"

namespace trinity::vision {

// Supported input/output extensions (lowercase, no dot).
bool isSupportedImageFormat(const std::string& ext);

struct ProcessedStats {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::vector<double> mean;
    std::vector<double> min;
    std::vector<double> max;
    double overallMean = 0.0;
    double overallMin = 0.0;
    double overallMax = 0.0;

    core::Json toJson() const;
};

class ImageProcessor {
public:
    // Validate path exists, extension supported, and OpenCV can decode
    // it. Throws core::RequestValidationError with structured details
    // on any failure — never returns success for an unreadable image.
    static void validateImagePath(const std::string& path);

    // Load + return metadata (width/height/channels/format/size).
    static ImageMetadata loadImage(const std::string& path);

    // Resize to width x height, write PNG/JPG to outputPath.
    // Throws on invalid dimensions or save failure.
    static ImageMetadata resizeImage(const std::string& srcPath, int width,
                                     int height, const std::string& outputPath);

    // Convert to single-channel grayscale, write to outputPath.
    static ImageMetadata grayscale(const std::string& srcPath,
                                   const std::string& outputPath);

    // Canny edge detection, write binary edge map to outputPath.
    // low/high are Canny thresholds (default 50/150 when omitted).
    static ImageMetadata edgeDetect(const std::string& srcPath, double low,
                                    double high, const std::string& outputPath);

    // Per-channel and overall statistics of the loaded image.
    static ProcessedStats imageStatistics(const std::string& srcPath);
};

}  // namespace trinity::vision
