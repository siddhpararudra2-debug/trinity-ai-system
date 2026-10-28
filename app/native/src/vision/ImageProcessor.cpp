#include "trinity/vision/ImageProcessor.hpp"

#include <algorithm>
#include <filesystem>

#include "trinity/core/Error.hpp"

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace trinity::vision {

core::Json ContourMeasurement::toJson() const {
    return {{"area_px2", areaPx},
            {"centroid_px", {{"x", centroidXPx}, {"y", centroidYPx}}},
            {"bbox_px",
             {{"x", bboxXPx}, {"y", bboxYPx}, {"w", bboxWPx}, {"h", bboxHPx}}}};
}

core::Json ContourResult::toJson() const {
    core::Json list = core::Json::array();
    for (const auto& c : contours) {
        list.push_back(c.toJson());
    }
    return {{"contour_count", contourCount}, {"contours", list}};
}

core::Json FeatureMatchResult::toJson() const {
    core::Json dists = core::Json::array();
    for (double d : matchDistances) {
        dists.push_back(d);
    }
    return {{"keypoints_a", keypointsA},
            {"keypoints_b", keypointsB},
            {"match_count", matchCount},
            {"match_distances", dists}};
}

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
#ifdef TRINITY_HAS_OPENCV
    if (!cv::haveImageReader(path)) {
        throw core::RequestValidationError("Image cannot be read or is corrupted", 
            {{"path", path}});
    }
#else
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build",
        {{"path", path}});
#endif
}

ImageMetadata ImageProcessor::loadImage(const std::string& path) {
    validateImagePath(path);
#ifndef TRINITY_HAS_OPENCV
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build",
        {{"path", path}});
#else
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
#endif
}

ImageMetadata ImageProcessor::resizeImage(const std::string& srcPath, int width,
                                          int height, const std::string& outputPath) {
    if (width <= 0 || height <= 0) {
        throw core::RequestValidationError("Invalid resize dimensions", 
            {{"width", width}, {"height", height}});
    }
#ifndef TRINITY_HAS_OPENCV
    (void)srcPath;
    (void)outputPath;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
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
#endif
}

ImageMetadata ImageProcessor::grayscale(const std::string& srcPath,
                                        const std::string& outputPath) {
#ifndef TRINITY_HAS_OPENCV
    (void)srcPath;
    (void)outputPath;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
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
#endif
}

ImageMetadata ImageProcessor::edgeDetect(const std::string& srcPath, double low,
                                         double high, const std::string& outputPath) {
#ifndef TRINITY_HAS_OPENCV
    (void)srcPath;
    (void)low;
    (void)high;
    (void)outputPath;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
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
#endif
}

ProcessedStats ImageProcessor::imageStatistics(const std::string& srcPath) {
#ifndef TRINITY_HAS_OPENCV
    (void)srcPath;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
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
#endif
}

ContourResult ImageProcessor::extractContours(const std::string& srcPath,
                                              double cannyLow, double cannyHigh,
                                              double minArea) {
#ifndef TRINITY_HAS_OPENCV
    (void)srcPath;
    (void)cannyLow;
    (void)cannyHigh;
    (void)minArea;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
    cv::Mat src = cv::imread(srcPath, cv::IMREAD_GRAYSCALE);
    if (src.empty()) {
        throw core::RequestValidationError("Failed to load source image for contour extraction",
                                           {{"path", srcPath}});
    }
    cv::Mat edges;
    cv::Canny(src, edges, cannyLow, cannyHigh);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    ContourResult out;
    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea) {
            continue;
        }
        cv::Moments m = cv::moments(contour);
        ContourMeasurement c;
        c.areaPx = area;
        c.centroidXPx = (m.m00 != 0.0) ? m.m10 / m.m00 : 0.0;
        c.centroidYPx = (m.m00 != 0.0) ? m.m01 / m.m00 : 0.0;
        const cv::Rect box = cv::boundingRect(contour);
        c.bboxXPx = box.x;
        c.bboxYPx = box.y;
        c.bboxWPx = box.width;
        c.bboxHPx = box.height;
        out.contours.push_back(c);
    }
    out.contourCount = static_cast<int>(out.contours.size());
    return out;
#endif
}

FeatureMatchResult ImageProcessor::matchFeatures(const std::string& pathA,
                                                 const std::string& pathB,
                                                 double distanceRatio) {
#ifndef TRINITY_HAS_OPENCV
    (void)pathA;
    (void)pathB;
    (void)distanceRatio;
    throw core::RequestValidationError(
        "Image processing requires OpenCV, which is not available in this build");
#else
    cv::Mat a = cv::imread(pathA, cv::IMREAD_GRAYSCALE);
    cv::Mat b = cv::imread(pathB, cv::IMREAD_GRAYSCALE);
    if (a.empty()) {
        throw core::RequestValidationError("Failed to load first image for feature matching",
                                           {{"path", pathA}});
    }
    if (b.empty()) {
        throw core::RequestValidationError("Failed to load second image for feature matching",
                                           {{"path", pathB}});
    }
    cv::Ptr<cv::ORB> orb = cv::ORB::create(2000);
    std::vector<cv::KeyPoint> kpa, kpb;
    cv::Mat da, db;
    orb->detectAndCompute(a, cv::noArray(), kpa, da);
    orb->detectAndCompute(b, cv::noArray(), kpb, db);

    FeatureMatchResult out;
    out.keypointsA = static_cast<int>(kpa.size());
    out.keypointsB = static_cast<int>(kpb.size());
    if (da.empty() || db.empty()) {
        return out;  // honest zero: ORB found no descriptors in one image
    }
    cv::BFMatcher matcher(cv::NORM_HAMMING);
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(da, db, knn, 2);
    for (const auto& pair : knn) {
        if (pair.size() < 2) {
            continue;
        }
        if (pair[0].distance < distanceRatio * pair[1].distance) {
            out.matchDistances.push_back(pair[0].distance);
        }
    }
    out.matchCount = static_cast<int>(out.matchDistances.size());
    return out;
#endif
}

}  // namespace trinity::vision
