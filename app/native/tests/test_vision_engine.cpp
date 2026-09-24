#include <doctest.h>
#include <algorithm>
#include <filesystem>

#include "trinity/engines/VisionEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/core/Paths.hpp"
#include "trinity/core/Uuid.hpp"

#ifdef TRINITY_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#endif

using namespace trinity::engines;
using namespace trinity::core;

namespace {
std::string tempPngPath() {
    std::string id = newUuid();
    id.erase(std::remove(id.begin(), id.end(), '-'), id.end());
    return (std::filesystem::temp_directory_path() / (id.substr(0, 8) + ".png")).string();
}
}  // namespace

TEST_CASE("VisionEngine describes capabilities") {
    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "describe";
    const auto res = engine.execute(req);
    CHECK(res.success);
}

#ifdef TRINITY_HAS_OPENCV
TEST_CASE("VisionEngine operations") {
    VisionEngine engine;
    
    std::string testImgPath = tempPngPath();
    cv::Mat dummy(100, 100, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::imwrite(testImgPath, dummy);
    
    SUBCASE("Load Image") {
        EngineRequest req;
        req.engine = "vision";
        req.operation = "load_image";
        req.parameters["path"] = testImgPath;
        
        EngineResult res = engine.execute(req);
        CHECK(res.success);
        CHECK(res.result["output"]["path"] == "");  // load_image generates no output file
        CHECK(res.result["input"]["metadata"]["width"] == 100);  // image actually loaded
    }
    
    SUBCASE("Resize Image") {
        EngineRequest req;
        req.engine = "vision";
        req.operation = "resize_image";
        req.parameters["path"] = testImgPath;
        req.parameters["width"] = 50;
        req.parameters["height"] = 50;
        
        EngineResult res = engine.execute(req);
        CHECK(res.success);
        CHECK(res.result["output"].is_object());
        CHECK(res.pendingArtifacts.size() == 1);
    }
    
    std::filesystem::remove(testImgPath);
}
#else
TEST_CASE("VisionEngine image ops refuse without OpenCV") {
    VisionEngine engine;
    EngineRequest req;
    req.engine = "vision";
    req.operation = "load_image";
    req.parameters["path"] = "anything.png";
    const auto res = engine.execute(req);
    CHECK_FALSE(res.success);
    REQUIRE_FALSE(res.errors.empty());
}
#endif
