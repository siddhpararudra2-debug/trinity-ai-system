// OpenCV depth ops: contour extraction and ORB feature matching,
// driving the engines with generated PNGs (no fixture binaries).

#include <doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "trinity/engines/VisionEngine.hpp"
#include "trinity/core/Uuid.hpp"
#include "trinity/vision/ImageProcessor.hpp"

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#endif

using namespace trinity::engines;

namespace {

std::string tempPng(const char* tag) {
    const std::string id = trinity::core::newUuid();
    return (std::filesystem::temp_directory_path() /
            (std::string(tag) + id.substr(0, 8) + ".png"))
        .string();
}

}  // namespace

#ifdef TRINITY_HAS_OPENCV

TEST_CASE("extract_contours finds generated shapes with areas and centroids") {
    // White square 60x60 at (20,20) and white circle r=25 at (160,140)
    // on a black 200x200 canvas.
    cv::Mat canvas = cv::Mat::zeros(200, 200, CV_8UC1);
    cv::rectangle(canvas, {20, 20, 60, 60}, cv::Scalar(255), -1);
    cv::circle(canvas, {160, 140}, 25, cv::Scalar(255), -1);
    const std::string path = tempPng("shapes");
    REQUIRE(cv::imwrite(path, canvas));

    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "extract_contours";
    req.parameters = {{"path", path}, {"min_area_px2", 50.0}};
    const auto res = engine.execute(req);
    REQUIRE(res.success);
    const auto stats = res.result["statistics"];
    CHECK(stats.value("contour_count", 0) == 2);

    // Square area 3600 px², centroid (50,50); circle area ≈ π·25².
    bool sawSquare = false;
    bool sawCircle = false;
    for (const auto& contour : stats["contours"]) {
        const double area = contour["area_px2"];
        const double cx = contour["centroid_px"]["x"];
        const double cy = contour["centroid_px"]["y"];
        if (std::fabs(cx - 50.0) < 2.0 && std::fabs(cy - 50.0) < 2.0) {
            sawSquare = true;
            CHECK(area == doctest::Approx(3600.0).epsilon(1e-2));
        } else if (std::fabs(cx - 160.0) < 2.0 && std::fabs(cy - 140.0) < 2.0) {
            sawCircle = true;
            CHECK(area == doctest::Approx(3.14159265358979323846 * 25.0 * 25.0)
                              .epsilon(2e-2));
        }
    }
    CHECK(sawSquare);
    CHECK(sawCircle);
    std::filesystem::remove(path);
}

TEST_CASE("extract_contours min_area filters specks") {
    cv::Mat canvas = cv::Mat::zeros(100, 100, CV_8UC1);
    cv::rectangle(canvas, {10, 10, 50, 50}, cv::Scalar(255), -1);  // 2500 px²
    cv::rectangle(canvas, {80, 80, 2, 2}, cv::Scalar(255), -1);    // 4 px²
    const std::string path = tempPng("specks");
    REQUIRE(cv::imwrite(path, canvas));

    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "extract_contours";
    req.parameters = {{"path", path}, {"min_area_px2", 100.0}};
    const auto res = engine.execute(req);
    REQUIRE(res.success);
    CHECK(res.result["statistics"].value("contour_count", 99) == 1);
    std::filesystem::remove(path);
}

TEST_CASE("match_features self-matches a generated textured image") {
    // Random gradients give ORB corners to latch onto; the flat test
    // images used elsewhere yield zero keypoints (also covered below).
    cv::Mat canvas = cv::Mat::zeros(220, 240, CV_8UC1);
    cv::theRNG().state = 42;  // deterministic rng
    for (int y = 0; y < canvas.rows; y += 8) {
        cv::rectangle(canvas, {0, y, 240, 4}, cv::Scalar((y * 7) % 255), -1);
    }
    for (int x = 0; x < canvas.cols; x += 16) {
        cv::rectangle(canvas, {x, 0, 4, 220}, cv::Scalar((x * 13) % 255), -1);
    }
    const std::string pathA = tempPng("featA");
    const std::string pathB = tempPng("featB");
    REQUIRE(cv::imwrite(pathA, canvas));
    // Second image = shifted copy of the first (same texture).
    cv::Mat shifted(220, 240, CV_8UC1, cv::Scalar(0));
    canvas(cv::Rect(0, 0, 240, 210)).copyTo(shifted(cv::Rect(0, 10, 240, 210)));
    REQUIRE(cv::imwrite(pathB, shifted));

    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "match_features";
    req.parameters = {{"path", pathA}, {"second_path", pathB}};
    const auto res = engine.execute(req);
    REQUIRE(res.success);
    const auto stats = res.result["statistics"];
    CHECK(stats.value("keypoints_a", 0) > 0);
    CHECK(stats.value("match_count", 0) > 0);
    std::filesystem::remove(pathA);
    std::filesystem::remove(pathB);
}

TEST_CASE("match_features reports honest zero on flat images") {
    cv::Mat flat(100, 100, CV_8UC1, cv::Scalar(100));
    const std::string pathA = tempPng("flatA");
    const std::string pathB = tempPng("flatB");
    REQUIRE(cv::imwrite(pathA, flat));
    REQUIRE(cv::imwrite(pathB, flat));

    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "match_features";
    req.parameters = {{"path", pathA}, {"second_path", pathB}};
    const auto res = engine.execute(req);
    REQUIRE(res.success);
    const auto stats = res.result["statistics"];
    CHECK(stats.value("keypoints_a", 0) == 0);
    CHECK(stats.value("keypoints_b", 0) == 0);
    CHECK(stats.value("match_count", 0) == 0);
    std::filesystem::remove(pathA);
    std::filesystem::remove(pathB);
}

#else

TEST_CASE("vision contour ops refuse without OpenCV") {
    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "extract_contours";
    req.parameters = {{"path", "anything.png"}};
    const auto res = engine.execute(req);
    CHECK_FALSE(res.success);
}

#endif
