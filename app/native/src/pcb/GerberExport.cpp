#include "trinity/pcb/GerberExport.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace trinity::pcb {

namespace {

// Mo=2 (abs), Nd=6: integers in 1e-4 mm after the %FSLAX26Y26*% header —
// radians of the four decimals match gerberprecision 4 in KiCad setup.
constexpr int kDecimals = 6;  // Nd (number of digits after implied point)

std::string gerberCoord(double mm) {
    // Scale into 1e-4 mm integers (4 fractional digits under a 2+6
    // format with point coordinates divided by 1e-2 per RS-274X FSLA).
    const long long value = std::llround(mm * 1e4);
    return std::to_string(value);
}

}  // namespace

core::Json GerberLayer::toJson() const {
    return core::Json{{"name", name}, {"file_name", fileName}, {"content", content}};
}

bool verifyGerber(const std::string& content, std::string& errorOut) {
    if (content.find("%FSLAX") == std::string::npos) {
        errorOut = "Missing %FSLAX format header";
        return false;
    }
    if (content.find("%MOMM*%") == std::string::npos) {
        errorOut = "Missing %MOMM*% unit header";
        return false;
    }
    if (content.find("%LPD*%") == std::string::npos) {
        errorOut = "Missing %LPD*% polarity header";
        return false;
    }
    size_t pos = 0;
    // Extended (block) commands: every % ... % pair opens and closes.
    size_t percentSigns = 0;
    for (char c : content) {
        if (c == '%') ++percentSigns;
    }
    if (percentSigns % 2 != 0) {
        errorOut = "Unbalanced extended command % pair";
        return false;
    }
    // Every command line ends with '*' or closes a block with '%'.
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.back() != '*' && line.back() != '%') {
            errorOut = "Command without terminator '*' or '%': " + line;
            return false;
        }
        (void)pos;
    }
    if (content.find("M02*") == std::string::npos) {
        errorOut = "Missing M02* end-of-program";
        return false;
    }
    return true;
}

GerberExport exportGerber(const PcbDesign& design,
                          const std::string& projectName) {
    GerberExport out;
    if (design.board.widthMm <= 0.0 || design.board.heightMm <= 0.0) {
        out.error = "Cannot export a board with non-positive dimensions";
        return out;
    }
    const std::string project =
        projectName.empty() ? std::string("trinity_pcb") : projectName;
    const double ox = design.board.originMm.x;
    const double oy = design.board.originMm.y;
    const double boardX1 = ox + design.board.widthMm;
    const double boardY1 = oy + design.board.heightMm;

    auto writeCommonHead = [](const std::string& origin) {
        std::ostringstream body;
        body << "G04 " << origin << "*\n";
        body << "%MOMM*%\n";
        body << "%LPD*%\n";
        body << "%FSLAX26Y26*%\n";
        body << "G01*\n";
        return body.str();
    };

    // --- Apertures used by the copper top layer ---
    std::map<std::string, int> traceAperture;  // width key -> D number
    int nextD = 10;
    for (const auto& track : design.tracks) {
        const std::string key = std::to_string(track.widthMm);
        if (traceAperture.find(key) == traceAperture.end()) {
            traceAperture[key] = nextD++;
        }
    }
    // Pad apertures: shape key -> D number (same shape+size statement).
    std::map<std::string, int> padAperture;
    for (const auto& place : design.placements) {
        const Component* comp = design.findComponent(place.ref);
        if (comp == nullptr) continue;
        const auto fp = design.footprints.find(comp->footprint);
        if (fp == design.footprints.end()) {
            out.error = "Component '" + comp->ref + "' uses unknown footprint '" +
                        comp->footprint + "'";
            return out;
        }
        for (const auto& pad : fp->second.pads) {
            const std::string key = pad.shape + ":" + std::to_string(pad.sizeMm.x) +
                                    ":" + std::to_string(pad.sizeMm.y);
            if (padAperture.find(key) == padAperture.end()) {
                padAperture[key] = nextD++;
            }
        }
    }
    const int outlineAperture = nextD;

    // --- Copper top layer file ---
    std::ostringstream copper;
    copper << writeCommonHead("project " + project + " copper F.Cu");
    copper << "G04 copper apertures*\n";
    for (const auto& [widthKey, dNum] : traceAperture) {
        copper << "%ADD" << dNum << "C," << widthKey << "*%\n";
    }
    for (const auto& [shapeKey, dNum] : padAperture) {
        const auto colon1 = shapeKey.find(':');
        const auto colon2 = shapeKey.find(':', colon1 + 1);
        const std::string shape = shapeKey.substr(0, colon1);
        const std::string w = shapeKey.substr(colon1 + 1, colon2 - colon1 - 1);
        const std::string h = shapeKey.substr(colon2 + 1);
        std::string macro;
        if (shape == "circle") {
            macro = "C," + w;  // circle aperture: diameter only
        } else if (shape == "oval") {
            macro = "O," + w + "X" + h;
        } else {
            macro = "R," + w + "X" + h;
        }
        copper << "%ADD" << dNum << macro << "*%\n";
    }
    copper << "G04 copper draws D01 (traces) and flashes D03 (pads)*\n";
    for (const auto& track : design.tracks) {
        const std::string widthKey = std::to_string(track.widthMm);
        const int aperture = traceAperture.at(widthKey);
        copper << "D" << aperture << "*\n"
               << "X" << gerberCoord(track.x1Mm) << "Y" << gerberCoord(track.y1Mm)
               << "D02*\n"
               << "X" << gerberCoord(track.x2Mm) << "Y" << gerberCoord(track.y2Mm)
               << "D01*\n";
    }
    for (const auto& place : design.placements) {
        const Component* comp = design.findComponent(place.ref);
        if (comp == nullptr) continue;
        const auto fp = design.footprints.find(comp->footprint);
        for (const auto& pad : fp->second.pads) {
            const std::string key = pad.shape + ":" + std::to_string(pad.sizeMm.x) +
                                    ":" + std::to_string(pad.sizeMm.y);
            const int aperture = padAperture.at(key);
            const double px = place.xMm + pad.posMm.x;
            const double py = place.yMm + pad.posMm.y;
            copper << "D" << aperture << "*\n"
                   << "X" << gerberCoord(px) << "Y" << gerberCoord(py) << "D03*\n";
        }
    }
    copper << "M02*\n";
    out.layers.push_back(GerberLayer{"F_Cu", project + "-F_Cu.gbr", copper.str()});

    // --- Board outline layer file (Edge.Cuts rectangle) ---
    std::ostringstream edge;
    edge << writeCommonHead("project " + project + " board outline Edge.Cuts");
    edge << "%ADD" << outlineAperture << "C,0.1*%\n"
         << "D" << outlineAperture << "*\n"
         << "X" << gerberCoord(ox) << "Y" << gerberCoord(oy) << "D02*\n"
         << "X" << gerberCoord(boardX1) << "Y" << gerberCoord(oy) << "D01*\n"
         << "X" << gerberCoord(boardX1) << "Y" << gerberCoord(boardY1) << "D01*\n"
         << "X" << gerberCoord(ox) << "Y" << gerberCoord(boardY1) << "D01*\n"
         << "X" << gerberCoord(ox) << "Y" << gerberCoord(oy) << "D01*\n"
         << "M02*\n";
    out.layers.push_back(
        GerberLayer{"Edge_Cuts", project + "-Edge_Cuts.gbr", edge.str()});

    // Self-verify every layer before reporting success — never ship
    // malformed manufacturing data.
    for (const auto& layer : out.layers) {
        std::string problem;
        if (!verifyGerber(layer.content, problem)) {
            out.layers.clear();
            out.error = "Generated Gerber for " + layer.name +
                        " failed self-verification: " + problem;
            return out;
        }
    }
    out.ok = true;
    return out;
}

}  // namespace trinity::pcb
