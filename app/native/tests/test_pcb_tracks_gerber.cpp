#include <doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "trinity/engines/PcbEngine.hpp"
#include "trinity/pcb/GerberExport.hpp"
#include "trinity/pcb/PcbDesign.hpp"
#include "trinity/pcb/Validators.hpp"

namespace fs = std::filesystem;

namespace {

trinity::engines::EngineRequest makeReq(const std::string& op,
                                        const trinity::core::Json& params) {
    trinity::engines::EngineRequest req;
    req.engine = "pcb";
    req.operation = op;
    req.parameters = params;
    return req;
}

trinity::pcb::PcbDesign simpleDesign() {
    trinity::pcb::PcbDesign design;
    design.board.widthMm = 30.0;
    design.board.heightMm = 40.0;
    trinity::pcb::Net netA;
    netA.id = 1;
    netA.name = "netA";
    netA.pins = {"U1.1"};
    design.nets.push_back(netA);
    trinity::pcb::Net netB;
    netB.id = 2;
    netB.name = "netB";
    design.nets.push_back(netB);
    trinity::pcb::Component comp;
    comp.ref = "U1";
    comp.value = "test";
    comp.footprint = "SOT-23";
    trinity::pcb::Pin pin;
    pin.number = "1";
    pin.name = "A";
    pin.posMm = {0.0, 0.0};
    pin.net = "netA";
    comp.pins.push_back(pin);
    design.components.push_back(comp);
    trinity::pcb::Footprint sot;
    sot.name = "SOT-23";
    sot.widthMm = 3.0;
    sot.heightMm = 3.0;
    trinity::pcb::FootprintPad pad;
    pad.number = "1";
    pad.posMm = {0.0, 0.0};
    pad.sizeMm = {0.5, 0.8};
    pad.shape = "rect";
    pad.kind = "smd";
    sot.pads.push_back(pad);
    design.footprints["SOT-23"] = sot;
    trinity::pcb::Placement place;
    place.ref = "U1";
    place.xMm = 10.0;
    place.yMm = 10.0;
    design.placements.push_back(place);
    return design;
}

trinity::pcb::TrackSegment track(const std::string& net, double x1, double y1,
                                 double x2, double y2, double width = 0.3) {
    trinity::pcb::TrackSegment t;
    t.net = net;
    t.widthMm = width;
    t.layer = "F.Cu";
    t.x1Mm = x1;
    t.y1Mm = y1;
    t.x2Mm = x2;
    t.y2Mm = y2;
    return t;
}

}  // namespace

TEST_CASE("track segment serializes and round-trips") {
    const trinity::pcb::TrackSegment original =
        track("netA", 1.5, 2.5, 8.75, 40.0, 0.25);
    const auto json = original.toJson();
    const auto restored = trinity::pcb::TrackSegment::fromJson(json);
    CHECK(restored.net == "netA");
    CHECK(restored.widthMm == doctest::Approx(0.25));
    CHECK(restored.x1Mm == doctest::Approx(1.5));
    CHECK(restored.y1Mm == doctest::Approx(2.5));
    CHECK(restored.x2Mm == doctest::Approx(8.75));
    CHECK(restored.y2Mm == doctest::Approx(40.0));
    CHECK(restored.layer == "F.Cu");

    // The whole design round-trips tracks too.
    trinity::pcb::PcbDesign design = simpleDesign();
    design.tracks.push_back(original);
    const auto restoredDesign =
        trinity::pcb::PcbDesign::fromJson(design.toJson());
    REQUIRE(restoredDesign.tracks.size() == 1);
    CHECK(restoredDesign.tracks.front().net == "netA");
    CHECK(restoredDesign.tracks.front().widthMm == doctest::Approx(0.25));
}

TEST_CASE("trace width below the design rule is rejected") {
    trinity::pcb::PcbDesign design = simpleDesign();
    design.tracks.push_back(track("netA", 0.0, 0.0, 10.0, 0.0, 0.05));
    const auto result = trinity::pcb::validatePcbDesign(design);
    CHECK_FALSE(result.passed());
    bool found = false;
    for (const auto& msg : result.messages) {
        if (msg.rule == "trace.width") {
            found = true;
            CHECK(msg.details.value("width_mm", 0.0) ==
                  doctest::Approx(0.05));
        }
    }
    CHECK(found);
}

TEST_CASE("inter-net clearance violation between tracks is rejected") {
    trinity::pcb::PcbDesign design = simpleDesign();
    // Parallel horizontal tracks: centers 0.15 mm apart with 0.25 mm
    // widths → the swept edge gap is −0.10 (overlap), far below the
    // 0.2 mm minimum; off-edge coords keep the board-bounds rule out.
    design.tracks.push_back(track("netA", 2.0, 5.0, 20.0, 5.0, 0.25));
    design.tracks.push_back(track("netB", 2.0, 5.15, 20.0, 5.15, 0.25));
    const auto result = trinity::pcb::validatePcbDesign(design);
    CHECK_FALSE(result.passed());
    bool found = false;
    for (const auto& msg : result.messages) {
        if (msg.rule == "trace.clearance") found = true;
    }
    CHECK(found);
}

TEST_CASE("same-net segments stacked meet (series routing is legal)") {
    trinity::pcb::PcbDesign design = simpleDesign();
    design.tracks.push_back(track("netA", 2.0, 5.0, 20.0, 5.0));
    design.tracks.push_back(track("netA", 20.0, 5.0, 20.0, 20.0));
    // Overlapping same-net segments must not matter (series routing).
    design.tracks.push_back(track("netA", 2.0, 5.0, 10.0, 5.0));
    const auto result = trinity::pcb::validatePcbDesign(design);
    CHECK(result.passed());
}

TEST_CASE("track leaving the board is rejected") {
    trinity::pcb::PcbDesign design = simpleDesign();
    design.tracks.push_back(track("netA", 28.0, 10.0, 40.0, 10.0));
    const auto result = trinity::pcb::validatePcbDesign(design);
    CHECK_FALSE(result.passed());
    bool found = false;
    for (const auto& msg : result.messages) {
        if (msg.rule == "trace.in_bounds") found = true;
    }
    CHECK(found);
}

TEST_CASE("segment distance helper handles touching parallel and crossing") {
    CHECK(trinity::pcb::trackSegmentDistanceMm(0, 0, 10, 0, 5, 0, 5, 5) ==
          doctest::Approx(0.0));  // T-junction touches
    CHECK(trinity::pcb::trackSegmentDistanceMm(0, 0, 10, 0, 0, 2, 10, 2) ==
          doctest::Approx(2.0));  // parallel
    CHECK(trinity::pcb::trackSegmentDistanceMm(0, 0, 10, 0, 5, 2, 5, 8) ==
          doctest::Approx(2.0));  // floor clearance of a true crossing
}

TEST_CASE("gerber file has RS-274X headers, apertures, draws and flashes") {
    trinity::pcb::PcbDesign design = simpleDesign();
    design.tracks.push_back(track("netA", 1.0, 2.0, 9.5, 2.0, 0.25));
    design.tracks.push_back(track("netB", 1.0, 5.0, 9.5, 5.0, 0.5));
    const auto gerber = trinity::pcb::exportGerber(design, "demo");
    REQUIRE(gerber.ok);
    REQUIRE(gerber.layers.size() == 2);
    const std::string& copper = gerber.layers[0].content;
    CHECK(gerber.layers[0].name == "F_Cu");
    CHECK(gerber.layers[0].fileName == "demo-F_Cu.gbr");
    CHECK(copper.find("%FSLAX26Y26*%") != std::string::npos);
    CHECK(copper.find("%MOMM*%") != std::string::npos);
    CHECK(copper.find("%LPD*%") != std::string::npos);
    CHECK(copper.find("%ADD10C,0.250000*%") != std::string::npos);
    CHECK(copper.find("%ADD11C,0.500000*%") != std::string::npos);
    CHECK(copper.find("D02*") != std::string::npos);
    CHECK(copper.find("D01*") != std::string::npos);
    // One pad flash: U1 placed at (10,10) with the pad at its origin,
    // so the flash lands at exactly 10.0 mm → 1e4-axis 100000.
    CHECK(copper.find("X100000Y100000D03*") != std::string::npos);
    CHECK(copper.rfind("M02*") == copper.size() - std::string("M02*\n").size());
    // One outline aperture + 4 D01 draws on Edge.Cuts.
    const std::string& edge = gerber.layers[1].content;
    CHECK(gerber.layers[1].name == "Edge_Cuts");
    CHECK(gerber.layers[1].fileName == "demo-Edge_Cuts.gbr");
    CHECK(edge.rfind("M02*") == edge.size() - std::string("M02*\n").size());
    std::string problem;
    CHECK(trinity::pcb::verifyGerber(copper, problem));
    CHECK(trinity::pcb::verifyGerber(gerber.layers[1].content, problem));
}

TEST_CASE("gerber self-verification rejects malformed output") {
    std::string err;
    CHECK_FALSE(trinity::pcb::verifyGerber("not gerber", err));
    CHECK_FALSE(trinity::pcb::verifyGerber(
        "%FSLAX26Y26*%\n%MOMM*%\n%LPD*%\nG01*\nX1000Y1000D01\n", err));
}

TEST_CASE("engine add_track builds tracks and refuses nonsense") {
    trinity::engines::PcbEngine engine;
    auto design =
        engine.execute(makeReq("create_board",
                               {{"name", "t"}, {"width_mm", 30.0},
                                {"height_mm", 40.0}}))
            .result["design"];
    // Unconnected track (empty net) accepted.
    REQUIRE(
        engine.execute(makeReq("add_track",
                               {{"design", design},
                                {"net", ""},
                                {"from", {{"x", 2.0}, {"y", 2.0}}},
                                {"to", {{"x", 10.0}, {"y", 2.0}}},
                                {"width_mm", 0.3}}))
            .success);
    // Unknown net refused.
    const auto bad = engine.execute(makeReq(
        "add_track", {{"design", design}, {"net", "ghost"},
                      {"from", {{"x", 2.0}, {"y", 5.0}}},
                      {"to", {{"x", 10.0}, {"y", 5.0}}}, {"width_mm", 0.3}}));
    CHECK_FALSE(bad.success);
    // Width below the rule minimum refused.
    const auto thin = engine.execute(makeReq(
        "add_track", {{"design", design}, {"net", ""},
                      {"from", {{"x", 2.0}, {"y", 8.0}}},
                      {"to", {{"x", 10.0}, {"y", 8.0}}}, {"width_mm", 0.05}}));
    CHECK_FALSE(thin.success);
}
