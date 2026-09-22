#include "trinity/pcb/KiCadExport.hpp"

#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>

#include "trinity/core/Uuid.hpp"

namespace trinity::pcb {

namespace {

// KiCad 7/8 stable board file version (readable by KiCad 8 as well).
constexpr const char* kFileVersion = "20221018";

std::string fmt(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    std::string text = out.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::string esc(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out += c;
        }
    }
    return out;
}

bool balanced(const std::string& content) {
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < content.size(); ++i) {
        const char c = content[i];
        if (inString) {
            if (c == '\\') {
                ++i;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (--depth < 0) {
                return false;
            }
        }
    }
    return !inString && depth == 0;
}

size_t countToken(const std::string& content, const std::string& token) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = content.find(token, pos)) != std::string::npos) {
        ++count;
        pos += token.size();
    }
    return count;
}

void writeLayers(std::ostringstream& out) {
    // Standard 2-layer stack subset (ids match KiCad layer numbering).
    static const std::pair<int, const char*> layers[] = {
        {0, "F.Cu"},      {31, "B.Cu"},    {32, "B.Adhes"}, {33, "F.Adhes"},
        {34, "B.Paste"},  {35, "F.Paste"}, {36, "B.SilkS"}, {37, "F.SilkS"},
        {38, "B.Mask"},   {39, "F.Mask"},  {40, "Dwgs.User"}, {41, "Cmts.User"},
        {42, "Eco1.User"}, {43, "Eco2.User"}, {44, "Edge.Cuts"}, {45, "Margin"},
        {46, "B.CrtYd"},  {47, "F.CrtYd"}, {48, "B.Fab"},   {49, "F.Fab"},
    };
    out << "  (layers\n";
    for (const auto& [id, name] : layers) {
        std::string kind = "user";
        if (std::string(name) == "F.Cu" || std::string(name) == "B.Cu") {
            kind = "signal";
        }
        out << "    (" << id << " \"" << name << "\" " << kind << ")\n";
    }
    out << "  )\n";
}

void writeSetup(std::ostringstream& out, double clearance) {
    out << "  (setup\n";
    out << "    (pad_to_mask_clearance 0)\n";
    out << "    (allow_missing_courtyard yes)\n";
    out << "    (trace_clearance " << fmt(clearance) << ")\n";
    out << "    (zone_clearance 0.508)\n";
    out << "    (trace_min 0.2)\n";
    out << "    (via_size 0.8)\n";
    out << "    (via_drill 0.4)\n";
    out << "    (uvia_size 0.3)\n";
    out << "    (uvia_drill 0.1)\n";
    out << "    (uvias_allowed no)\n";
    out << "    (vias_allowed yes)\n";
    out << "    (pcbplotparams (layerselection 0x00010fc_ffffffff) (plot_on_all_layers_selection "
           "0x0000000_00000000) (disableapertmacros no) (usegerberexclusions no) "
           "(usegerberaser no) (creategerberjobfile yes) (dashed_lines_dash_length 0.12) "
           "(dashed_lines_gap_length 0.12) (svgprecision 4) (plotframeref no) (viasonmask no) "
           "(mode 1) (useauxorigin no) (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15) "
           "(pdf_front_fp_property_popups no) (pdf_back_fp_property_popups no) "
           "(dxfpolygonmode yes) (dxfimptPcbText no) (dxfusepcbnewfont "
           "yes) (postscriptnegatenoxelf no) (gerberprecision 4) (gerberApertures no) "
           "(subtractmaskfromsilk no) (outputformat 1) (mirror no) (drillshape 1) "
           "(scaleselection 1) (outputdirectory \"\"))\n";
    out << "  )\n";
}

}  // namespace

bool verifyKicadPcb(const std::string& content, std::string& errorOut) {
    if (content.rfind("(kicad_pcb", 0) != 0) {
        errorOut = "Missing (kicad_pcb root element";
        return false;
    }
    if (!balanced(content)) {
        errorOut = "Unbalanced parentheses or unterminated string";
        return false;
    }
    for (const char* required :
         {"(version", "(generator", "(general", "(paper", "(layers", "(setup", "(net ",
          "(gr_rect", "Edge.Cuts"}) {
        if (content.find(required) == std::string::npos) {
            errorOut = std::string("Missing required section: ") + required;
            return false;
        }
    }
    return true;
}

KicadExport exportKicadPcb(const PcbDesign& design, const std::string& projectName) {
    KicadExport out;
    if (design.board.widthMm <= 0.0 || design.board.heightMm <= 0.0) {
        out.error = "Cannot export a board with non-positive dimensions";
        return out;
    }
    const std::string project = projectName.empty() ? "trinity_pcb" : projectName;

    // Net numbering: 0 is the KiCad unconnected placeholder.
    std::map<std::string, int> netIds;
    int nextId = 1;
    for (const auto& net : design.nets) {
        if (netIds.find(net.name) == netIds.end()) {
            netIds[net.name] = nextId++;
        }
    }

    std::ostringstream pcb;
    pcb << "(kicad_pcb (version " << kFileVersion << ") (generator trinity)\n";
    pcb << "  (general (thickness " << fmt(design.board.thicknessMm) << "))\n";
    pcb << "  (paper \"A4\")\n";
    pcb << "  (title_block (title \"" << esc(project) << "\") (date \"\") (rev \"\") "
           "(company \"\") (comment 1 \"\") (comment 2 \"\") (comment 3 \"\") (comment 4 \"\"))\n";
    writeLayers(pcb);
    const double clearance =
        design.rules.empty() ? 0.2 : design.rules.front().minClearanceMm;
    writeSetup(pcb, clearance);

    pcb << "  (net 0 \"\")\n";
    for (const auto& [name, id] : netIds) {
        pcb << "  (net " << id << " \"" << esc(name) << "\")\n";
    }

    size_t footprintsWritten = 0;
    for (const auto& place : design.placements) {
        const Component* comp = design.findComponent(place.ref);
        if (comp == nullptr) {
            out.error = "Placement references unknown component '" + place.ref + "'";
            return out;
        }
        const auto fp = design.footprints.find(comp->footprint);
        if (fp == design.footprints.end()) {
            out.error = "Component '" + comp->ref + "' uses unknown footprint '" +
                        comp->footprint + "'";
            return out;
        }
        const std::string layer =
            place.layer.empty() ? std::string("F.Cu") : place.layer;
        pcb << "  (footprint \"" << esc(fp->second.name) << "\" (layer \"" << esc(layer)
            << "\") (uuid \"" << core::newUuid() << "\") (at " << fmt(place.xMm) << " "
            << fmt(place.yMm) << " " << fmt(place.rotationDeg) << ")\n";
        pcb << "    (descr \"Trinity " << esc(fp->second.name) << "\")\n";
        pcb << "    (property \"Reference\" \"" << esc(comp->ref) << "\" (at 0 -3 0) "
               "(layer \"F.SilkS\") (uuid \"" << core::newUuid()
               << "\") (effects (font (size 1 1) (thickness 0.15))))\n";
        pcb << "    (property \"Value\" \"" << esc(comp->value) << "\" (at 0 3 0) "
               "(layer \"F.Fab\") (uuid \"" << core::newUuid()
               << "\") (effects (font (size 1 1) (thickness 0.15))))\n";
        // Pin net lookup for copper connectivity.
        std::map<std::string, std::string> pinNet;
        for (const auto& pin : comp->pins) {
            if (!pin.net.empty()) {
                pinNet[pin.number] = pin.net;
            }
        }
        for (const auto& pad : fp->second.pads) {
            const std::string kind = pad.kind == "thru_hole" ? "thru_hole" : "smd";
            const std::string shape =
                (pad.shape == "circle" || pad.shape == "oval") ? pad.shape : "rect";
            pcb << "    (pad \"" << esc(pad.number) << "\" " << kind << " " << shape
                << " (at " << fmt(pad.posMm.x) << " " << fmt(pad.posMm.y) << ") (size "
                << fmt(pad.sizeMm.x) << " " << fmt(pad.sizeMm.y) << ") (layers \""
                << esc(layer) << "\"";
            if (layer == "F.Cu") {
                pcb << " \"F.Paste\" \"F.Mask\"";
            } else {
                pcb << " \"B.Paste\" \"B.Mask\"";
            }
            const auto nit = pinNet.find(pad.number);
            if (nit != pinNet.end()) {
                const auto id = netIds.find(nit->second);
                if (id != netIds.end()) {
                    pcb << ") (net " << id->second << " \"" << esc(nit->second) << "\")";
                } else {
                    pcb << ")";
                }
            } else {
                pcb << ")";
            }
            pcb << " (pinfunction \"" << esc(pad.number) << "\") (pintype \"passive\") "
                   "(uuid \"" << core::newUuid() << "\")";
            if (kind == "thru_hole") {
                pcb << " (drill 0.8)";
            }
            pcb << ")\n";
        }
        pcb << "  )\n";
        ++footprintsWritten;
    }

    // Board outline on Edge.Cuts.
    const double ox = design.board.originMm.x;
    const double oy = design.board.originMm.y;
    pcb << "  (gr_rect (start " << fmt(ox) << " " << fmt(oy) << ") (end "
        << fmt(ox + design.board.widthMm) << " " << fmt(oy + design.board.heightMm)
        << ") (stroke (width 0.1) (type solid)) (fill none) (layer \"Edge.Cuts\") "
           "(uuid \"" << core::newUuid() << "\"))\n";
    pcb << ")\n";

    out.content = pcb.str();
    std::string problem;
    if (!verifyKicadPcb(out.content, problem)) {
        out.content.clear();
        out.error = "Generated board failed self-verification: " + problem;
        return out;
    }
    if (countToken(out.content, "(footprint \"") != footprintsWritten) {
        out.content.clear();
        out.error = "Footprint count mismatch after export";
        return out;
    }
    out.ok = true;
    return out;
}

}  // namespace trinity::pcb
