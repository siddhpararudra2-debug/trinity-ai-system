#include <doctest.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

#include "trinity/vision/ImageProcessor.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/core/Paths.hpp"
#include "trinity/core/Uuid.hpp"

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

using namespace trinity::vision;
using namespace trinity::core;

namespace {
std::string tempPngPath() {
    std::string id = newUuid();
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    return (std::filesystem::temp_directory_path() / (id.substr(0, 8) + ".png")).string();
}
}  // namespace

TEST_CASE("ImageProcessor validation") {
    SUBCASE("Supported formats") {
        CHECK(isSupportedImageFormat("png"));
        CHECK(isSupportedImageFormat(".jpg"));
        CHECK(isSupportedImageFormat("JPEG"));
        CHECK(!isSupportedImageFormat("txt"));
    }
}

#ifdef TRINITY_HAS_OPENCV
TEST_CASE("ImageProcessor functions") {
    std::string testImgPath = tempPngPath();
    
    // Create a dummy image
    cv::Mat dummy(100, 100, CV_8UC3, cv::Scalar(0, 0, 255));
    cv::imwrite(testImgPath, dummy);
    
    SUBCASE("Load Image") {
        ImageMetadata meta = ImageProcessor::loadImage(testImgPath);
        CHECK(meta.width == 100);
        CHECK(meta.height == 100);
        CHECK(meta.channels == 3);
        CHECK(meta.format == "png");
    }
    
    SUBCASE("Resize Image") {
        std::string outPath = tempPngPath();
        ImageMetadata meta = ImageProcessor::resizeImage(testImgPath, 50, 50, outPath);
        CHECK(meta.width == 50);
        CHECK(meta.height == 50);
        std::filesystem::remove(outPath);
    }
    
    SUBCASE("Image Statistics") {
        ProcessedStats stats = ImageProcessor::imageStatistics(testImgPath);
        CHECK(stats.width == 100);
        CHECK(stats.height == 100);
        CHECK(stats.channels == 3);
        REQUIRE(stats.mean.size() == 3);
        // B, G, R
        CHECK(stats.mean[0] == doctest::Approx(0));
        CHECK(stats.mean[1] == doctest::Approx(0));
        CHECK(stats.mean[2] == doctest::Approx(255));
    }
    
    std::filesystem::remove(testImgPath);
}
#else
TEST_CASE("ImageProcessor refuses without OpenCV") {
    CHECK_THROWS_AS(ImageProcessor::loadImage("missing.png"), RequestValidationError);
}
#endif
