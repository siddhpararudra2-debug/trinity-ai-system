#pragma once

// KiCad 7/8 s-expression exporter. This is the ONLY module that knows
// KiCad file syntax; the IR stays tool-agnostic. Output targets the
// stable `(kicad_pcb (version 20221018) ...)` layout readable by KiCad 7
// and 8. Every export is self-verified (balanced syntax + required
// sections + footprint/net counts) before it is handed to the caller —
// a failed verification is a truthful error, never a fake file.

#include <string>

#include "PcbDesign.hpp"

namespace trinity::pcb {

struct KicadExport {
    bool ok = false;
    std::string content;  // full .kicad_pcb text when ok
    std::string error;  // human-readable reason when !ok
};

/// Render a validated design to KiCad s-expression text.
KicadExport exportKicadPcb(const PcbDesign& design, const std::string& projectName);

/// Structural check used by the exporter and the tests: balanced
/// parentheses outside quoted strings plus required top-level sections.
bool verifyKicadPcb(const std::string& content, std::string& errorOut);

}  // namespace trinity::pcb
