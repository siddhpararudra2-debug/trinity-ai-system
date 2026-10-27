#pragma once

// Gerber RS-274X manufacturing export. Produces the copper layer (all
// F.Cu track segments as standard D01 draws on defined apertures), pad
// flashes (D03) per placed footprint, and the board outline on its own
// Edge.Cuts file. Coordinates use Mo=2 / Nd=6 (integer micro-mm-4
// truncation), millimetres, absolute mode — the intersection
// convention most CAM tools accept. Self-verification rejects
// malformed output rather than shipping it.

#include <string>
#include <vector>

#include "PcbDesign.hpp"

namespace trinity::pcb {

struct GerberLayer {
    std::string name;      // e.g. "F_Cu"
    std::string fileName;  // e.g. "<project>-F_Cu.gbr"
    std::string content;

    core::Json toJson() const;
};

struct GerberExport {
    std::vector<GerberLayer> layers;
    bool ok = false;
    std::string error;  // human reason when !ok
};

/// RS-274X export of the copper top layer and board outline.
GerberExport exportGerber(const PcbDesign& design,
                          const std::string& projectName);

/// Syntax check: header commands, aperture definitions, balanced
/// `%...%` blocks, every command ends with '*' , M02 terminator.
bool verifyGerber(const std::string& content, std::string& errorOut);

}  // namespace trinity::pcb
