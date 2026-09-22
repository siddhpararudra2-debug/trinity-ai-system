#include "trinity/vision/ImageProcessor.hpp"

#include <algorithm>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "trinity/core/Error.hpp"

namespace trinity::vision {

bool isSupportedImageFormat(const std::string& ext) {
    auto e = ext;
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    if (!e.empty() && e[0] == '.') e = e.substr(1);
    return e == "png" || e == "jpg" || e == "jpeg" || e == "bmp";
}

core::Json ProcessedStats::toJson() const {
    return {
        {"width", width},
        {"height", height},
        {"channels", channels},
        {"mean", mean},
        {"min", min},
        {"max", max},
        {"overall_mean", overallMean},
        {"overall_min", overallMin},
        {"overall_max", overallMax}
    };
}

void ImageProcessor::validateImagePath(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        throw core::RequestValidationError("Image file does not exist", 
            {{"path", path}});
    }
    std::string ext = std::filesystem::path(path).extension().string();
    if (!isSupportedImageFormat(ext)) {
        throw core::RequestValidationError("Unsupported image format", 
            {{"path", path}, {"format", ext}});
    }
    
    // Validate it's actually readable by OpenCV
    if (!cv::haveImageReader(path)) {
        throw core::RequestValidationError("Image cannot be read or is corrupted", 
            {{"path", path}});
    }
    
    // Quick load just to check it's not totally broken. 
    // Optimization: could just read header, but haveImageReader covers most cases.
    // We'll trust haveImageReader to save time on huge images.
}

ImageMetadata ImageProcessor::loadImage(const std::string& path) {
    validateImagePath(path);
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty()) {
        throw core::RequestValidationError("Failed to decode image data", {{"path", path}});
    }
    
    ImageMetadata meta;
    meta.width = img.cols;
    meta.height = img.rows;
    meta.channels = img.channels();
    std::string ext = std::filesystem::path(path).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    meta.format = ext;
    meta.sizeBytes = std::filesystem::file_size(path);
    return meta;
}

ImageMetadata ImageProcessor::resizeImage(const std::string& srcPath, int width,
                                          int height, const std::string& outputPath) {
    if (width <= 0 || height <= 0) {
        throw core::RequestValidationError("Invalid resize dimensions", 
            {{"width", width}, {"height", height}});
    }
    
    cv::Mat src = cv::imread(srcPath, cv::IMREAD_UNCHANGED);
    if (src.empty()) {
        throw core::RequestValidationError("Failed to load source image for resize", {{"path", srcPath}});
    }
    
    cv::Mat dst;
    cv::resize(src, dst, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
    
    if (!cv::imwrite(outputPath, dst)) {
        throw core::RequestValidationError("Failed to write resized image", {{"output_path", outputPath}});
    }
    
    ImageMetadata meta;
    meta.width = dst.cols;
    meta.height = dst.rows;
    meta.channels = dst.channels();
    std::string ext = std::filesystem::path(outputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    meta.format = ext;
    meta.sizeBytes = std::filesystem::file_size(outputPath);
    return meta;
}

ImageMetadata ImageProcessor::grayscale(const std::string& srcPath,
                                        const std::string& outputPath) {
    cv::Mat src = cv::imread(srcPath, cv::IMREAD_UNCHANGED);
    if (src.empty()) {
        throw core::RequestValidationError("Failed to load source image for grayscale", {{"path", srcPath}});
    }
    
    cv::Mat dst;
    if (src.channels() == 1) {
        dst = src; // Already grayscale
    } else if (src.channels() == 3) {
        cv::cvtColor(src, dst, cv::COLOR_BGR2GRAY);
    } else if (src.channels() == 4) {
        cv::cvtColor(src, dst, cv::COLOR_BGRA2GRAY);
    } else {
        throw core::RequestValidationError("Unsupported number of channels for grayscale", 
            {{"channels", src.channels()}});
    }
    
    if (!cv::imwrite(outputPath, dst)) {
        throw core::RequestValidationError("Failed to write grayscale image", {{"output_path", outputPath}});
    }
    
    ImageMetadata meta;
    meta.width = dst.cols;
    meta.height = dst.rows;
    meta.channels = dst.channels();
    std::string ext = std::filesystem::path(outputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    meta.format = ext;
    meta.sizeBytes = std::filesystem::file_size(outputPath);
    return meta;
}

ImageMetadata ImageProcessor::edgeDetect(const std::string& srcPath, double low,
                                         double high, const std::string& outputPath) {
    cv::Mat src = cv::imread(srcPath, cv::IMREAD_GRAYSCALE);
    if (src.empty()) {
        throw core::RequestValidationError("Failed to load source image for edge detection", {{"path", srcPath}});
    }
    
    cv::Mat edges;
    cv::Canny(src, edges, low, high);
    
    if (!cv::imwrite(outputPath, edges)) {
        throw core::RequestValidationError("Failed to write edge detection image", {{"output_path", outputPath}});
    }
    
    ImageMetadata meta;
    meta.width = edges.cols;
    meta.height = edges.rows;
    meta.channels = edges.channels();
    std::string ext = std::filesystem::path(outputPath).extension().string();
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    meta.format = ext;
    meta.sizeBytes = std::filesystem::file_size(outputPath);
    return meta;
}

ProcessedStats ImageProcessor::imageStatistics(const std::string& srcPath) {
    cv::Mat src = cv::imread(srcPath, cv::IMREAD_UNCHANGED);
    if (src.empty()) {
        throw core::RequestValidationError("Failed to load source image for statistics", {{"path", srcPath}});
    }
    
    ProcessedStats stats;
    stats.width = src.cols;
    stats.height = src.rows;
    stats.channels = src.channels();
    
    std::vector<cv::Mat> channels;
    cv::split(src, channels);
    
    stats.mean.resize(stats.channels);
    stats.min.resize(stats.channels);
    stats.max.resize(stats.channels);
    
    for (int i = 0; i < stats.channels; ++i) {
        cv::Scalar mean, stddev;
        cv::meanStdDev(channels[i], mean, stddev);
        stats.mean[i] = mean[0];
        
        double minVal, maxVal;
        cv::minMaxLoc(channels[i], &minVal, &maxVal);
        stats.min[i] = minVal;
        stats.max[i] = maxVal;
    }
    
    if (stats.channels > 0) {
        cv::Mat flat = src.reshape(1, 1);
        cv::Scalar mean, stddev;
        cv::meanStdDev(flat, mean, stddev);
        stats.overallMean = mean[0];
        
        double minVal, maxVal;
        cv::minMaxLoc(flat, &minVal, &maxVal);
        stats.overallMin = minVal;
        stats.overallMax = maxVal;
    }
    
    return stats;
}

}  // namespace trinity::vision
