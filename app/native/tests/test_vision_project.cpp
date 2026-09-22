#include <doctest/doctest.h>

#include "trinity/vision/VisionProject.hpp"

using namespace trinity::vision;

TEST_CASE("VisionProject serialization") {
    VisionProject vp;
    vp.name = "test_project";
    vp.source.path = "/test/image.png";
    vp.source.metadata.width = 1920;
    vp.source.metadata.height = 1080;
    
    Detection d;
    d.label = "person";
    d.confidence = 0.95;
    d.box = {10, 20, 100, 200};
    vp.detections.push_back(d);
    
    auto j = vp.toJson();
    auto vp2 = VisionProject::fromJson(j);
    
    CHECK(vp2.name == "test_project");
    CHECK(vp2.source.path == "/test/image.png");
    CHECK(vp2.source.metadata.width == 1920);
    CHECK(vp2.source.metadata.height == 1080);
    REQUIRE(vp2.detections.size() == 1);
    CHECK(vp2.detections[0].label == "person");
    CHECK(vp2.detections[0].confidence == doctest::Approx(0.95));
    CHECK(vp2.detections[0].box.x == 10);
    CHECK(vp2.detections[0].box.y == 20);
    CHECK(vp2.detections[0].box.width == 100);
    CHECK(vp2.detections[0].box.height == 200);
}
