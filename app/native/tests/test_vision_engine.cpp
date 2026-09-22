#include <doctest/doctest.h>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "trinity/engines/VisionEngine.hpp"
#include "trinity/engines/EngineRegistry.hpp"
#include "trinity/core/Error.hpp"
#include "trinity/core/Paths.hpp"
#include "trinity/core/Uuid.hpp"

using namespace trinity::engines;
using namespace trinity::core;

TEST_CASE("VisionEngine operations") {
    VisionEngine engine;
    
    std::string testImgPath = (std::filesystem::path(Paths::scratchDir()) / (Uuid::v4() + ".png")).string();
    cv::Mat dummy(100, 100, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::imwrite(testImgPath, dummy);
    
    SUBCASE("Load Image") {
        EngineRequest req;
        req.engine = "vision";
        req.operation = "load_image";
        req.parameters["path"] = testImgPath;
        
        EngineResult res = engine.execute(req);
        CHECK(res.success);
        CHECK(res.result["output"].is_null()); // no output image generated
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
