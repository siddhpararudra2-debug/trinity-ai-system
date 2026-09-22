#include "trinity/pcb/Validators.hpp"

#include <cmath>
#include <set>

namespace trinity::pcb {

namespace {

void addRule(validation::ValidationResult& out, const std::string& rule,
             validation::Severity severity, bool passed, const std::string& message,
             const core::Json& details = core::Json::object()) {
    validation::ValidationMessage msg;
    msg.rule = rule;
    msg.severity = severity;
    msg.passed = passed;
    msg.message = message;
    msg.details = details;
    out.addMessage(std::move(msg));
}

bool validRotation(double rotation) {
    for (double allowed : {0.0, 90.0, 180.0, 270.0}) {
        if (std::fabs(rotation - allowed) < 1e-9) {
            return true;
        }
    }
    return false;
}

/// Axis-aligned body box of a placed component (mm, board coordinates).
/// Returns false when the footprint is unknown and no box can be formed.
bool placedBox(const PcbDesign& design, const Placement& place, double& x0, double& y0,
               double& x1, double& y1) {
    const Component* comp = design.findComponent(place.ref);
    if (comp == nullptr) {
        return false;
    }
    const auto fp = design.footprints.find(comp->footprint);
    if (fp == design.footprints.end() || fp->second.widthMm <= 0.0 ||
        fp->second.heightMm <= 0.0) {
        return false;
    }
    double w = fp->second.widthMm;
    double h = fp->second.heightMm;
    const double rot = std::fmod(std::fabs(place.rotationDeg), 360.0);
    if (std::fabs(rot - 90.0) < 1e-9 || std::fabs(rot - 270.0) < 1e-9) {
        std::swap(w, h);
    }
    x0 = place.xMm - w / 2.0;
    y0 = place.yMm - h / 2.0;
    x1 = place.xMm + w / 2.0;
    y1 = place.yMm + h / 2.0;
    return true;
}

}  // namespace

validation::ValidationResult validatePcbDesign(const PcbDesign& design) {
    validation::ValidationResult out;
    out.operation = "validate_design";
    out.checks = core::Json::object();
    bool ok = true;

    // --- Board dimensions ---
    if (design.board.widthMm > 0.0 && design.board.heightMm > 0.0) {
        addRule(out, "board.dimensions", validation::Severity::Info, true,
                "Board dimensions are positive",
                {{"width_mm", design.board.widthMm},
                 {"height_mm", design.board.heightMm}});
    } else {
        ok = false;
        addRule(out, "board.dimensions", validation::Severity::Error, false,
                "Board width and height must be positive",
                {{"width_mm", design.board.widthMm},
                 {"height_mm", design.board.heightMm}});
    }
    if (design.board.thicknessMm > 0.0 && design.board.thicknessMm <= 10.0) {
        addRule(out, "board.thickness", validation::Severity::Info, true,
                "Board thickness is within 0–10 mm",
                {{"thickness_mm", design.board.thicknessMm}});
    } else {
        ok = false;
        addRule(out, "board.thickness", validation::Severity::Error, false,
                "Board thickness must be in (0, 10] mm",
                {{"thickness_mm", design.board.thicknessMm}});
    }

    // --- Reference / pin uniqueness ---
    {
        std::set<std::string> refs;
        for (const auto& comp : design.components) {
            if (comp.ref.empty()) {
                ok = false;
                addRule(out, "component.ref_present", validation::Severity::Error, false,
                        "Component with empty reference designator",
                        {{"value", comp.value}});
                continue;
            }
            if (!refs.insert(comp.ref).second) {
                ok = false;
                addRule(out, "component.ref_unique", validation::Severity::Error, false,
                        "Duplicate reference designator '" + comp.ref + "'",
                        {{"ref", comp.ref}});
            }
        }
        if (!design.components.empty() && ok) {
            addRule(out, "component.ref_unique", validation::Severity::Info, true,
                    "Reference designators are unique");
        }
        for (const auto& comp : design.components) {
            std::set<std::string> nums;
            for (const auto& pin : comp.pins) {
                if (!nums.insert(pin.number).second) {
                    ok = false;
                    addRule(out, "component.pin_unique", validation::Severity::Error,
                            false,
                            "Duplicate pin '" + pin.number + "' on '" + comp.ref + "'",
                            {{"ref", comp.ref}, {"pin", pin.number}});
                }
            }
            if (!std::isfinite(comp.rotationDeg) || !validRotation(comp.rotationDeg)) {
                ok = false;
                addRule(out, "component.rotation", validation::Severity::Error, false,
                        "Rotation of '" + comp.ref + "' must be one of 0/90/180/270",
                        {{"ref", comp.ref}, {"rotation_deg", comp.rotationDeg}});
            }
        }
    }

    // --- Nets reference real pins; power pins must be connected ---
    {
        std::set<std::string> knownPins;
        for (const auto& comp : design.components) {
            for (const auto& pin : comp.pins) {
                knownPins.insert(comp.ref + "." + pin.number);
            }
        }
        std::set<std::string> nettedPins;
        for (const auto& net : design.nets) {
            std::set<std::string> seen;
            for (const auto& pinRef : net.pins) {
                if (!seen.insert(pinRef).second) {
                    ok = false;
                    addRule(out, "net.pin_unique", validation::Severity::Error, false,
                            "Duplicate pin '" + pinRef + "' in net '" + net.name + "'",
                            {{"net", net.name}, {"pin", pinRef}});
                }
                std::string ref, num;
                if (!splitPinRef(pinRef, ref, num) ||
                    knownPins.find(pinRef) == knownPins.end()) {
                    ok = false;
                    addRule(out, "net.pin_known", validation::Severity::Error, false,
                            "Net '" + net.name + "' references unknown pin '" + pinRef +
                                "'",
                            {{"net", net.name}, {"pin", pinRef}});
                } else {
                    nettedPins.insert(pinRef);
                }
            }
        }
        for (const auto& comp : design.components) {
            for (const auto& pin : comp.pins) {
                const bool isPower = pin.electrical == PinElectricalType::PowerIn ||
                                     pin.electrical == PinElectricalType::PowerOut;
                if (isPower &&
                    nettedPins.find(comp.ref + "." + pin.number) == nettedPins.end()) {
                    ok = false;
                    addRule(out, "net.power_connected", validation::Severity::Error,
                            false,
                            "Power pin '" + comp.ref + "." + pin.number + "' (" +
                                pin.name + ") is not connected to any net",
                            {{"ref", comp.ref}, {"pin", pin.number}});
                }
            }
        }
        if (ok) {
            addRule(out, "net.consistent", validation::Severity::Info, true,
                    "Nets reference known pins and power pins are connected");
        }
    }

    // --- Placements: known refs, sane coordinates/rotations, in bounds ---
    {
        for (const auto& place : design.placements) {
            if (design.findComponent(place.ref) == nullptr) {
                ok = false;
                addRule(out, "placement.ref_known", validation::Severity::Error, false,
                        "Placement references unknown component '" + place.ref + "'",
                        {{"ref", place.ref}});
                continue;
            }
            if (!std::isfinite(place.xMm) || !std::isfinite(place.yMm)) {
                ok = false;
                addRule(out, "placement.coordinates", validation::Severity::Error,
                        false, "Placement of '" + place.ref + "' is not finite",
                        {{"ref", place.ref}});
                continue;
            }
            if (!validRotation(place.rotationDeg)) {
                ok = false;
                addRule(out, "placement.rotation", validation::Severity::Error, false,
                        "Rotation of '" + place.ref + "' must be one of 0/90/180/270",
                        {{"ref", place.ref}});
            }
            double x0, y0, x1, y1;
            if (placedBox(design, place, x0, y0, x1, y1)) {
                const double ox = design.board.originMm.x;
                const double oy = design.board.originMm.y;
                if (x0 < ox - 1e-9 || y0 < oy - 1e-9 ||
                    x1 > ox + design.board.widthMm + 1e-9 ||
                    y1 > oy + design.board.heightMm + 1e-9) {
                    ok = false;
                    addRule(out, "placement.in_bounds", validation::Severity::Error,
                            false,
                            "Component '" + place.ref + "' extends outside the board",
                            {{"ref", place.ref}});
                }
            }
        }
        // Unplaced components are a warning, not an error.
        for (const auto& comp : design.components) {
            if (design.findPlacement(comp.ref) == nullptr) {
                addRule(out, "placement.present", validation::Severity::Warning, true,
                        "Component '" + comp.ref + "' has no placement yet",
                        {{"ref", comp.ref}});
            }
        }
    }

    // --- Overlap + clearance where geometry exists ---
    {
        const double clearance =
            design.rules.empty() ? 0.2 : design.rules.front().minClearanceMm;
        for (size_t i = 0; i < design.placements.size(); ++i) {
            double ax0, ay0, ax1, ay1;
            if (!placedBox(design, design.placements[i], ax0, ay0, ax1, ay1)) {
                continue;
            }
            for (size_t j = i + 1; j < design.placements.size(); ++j) {
                double bx0, by0, bx1, by1;
                if (!placedBox(design, design.placements[j], bx0, by0, bx1, by1)) {
                    continue;
                }
                const bool overlap = ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1;
                if (overlap) {
                    ok = false;
                    addRule(out, "placement.no_overlap", validation::Severity::Error,
                            false,
                            "Components '" + design.placements[i].ref + "' and '" +
                                design.placements[j].ref + "' overlap",
                            {{"a", design.placements[i].ref},
                             {"b", design.placements[j].ref}});
                    continue;
                }
                const double gapX = std::max(0.0, std::max(bx0 - ax1, ax0 - bx1));
                const double gapY = std::max(0.0, std::max(by0 - ay1, ay0 - by1));
                // Corner-to-corner separation: an edge gap on either axis
                // counts (overlap is handled above).
                const double gap = std::sqrt(gapX * gapX + gapY * gapY);
                if (gap < clearance) {
                    ok = false;
                    addRule(out, "placement.clearance", validation::Severity::Error,
                            false,
                            "Clearance between '" + design.placements[i].ref +
                                "' and '" + design.placements[j].ref +
                                "' is below minimum",
                            {{"a", design.placements[i].ref},
                             {"b", design.placements[j].ref},
                             {"gap_mm", gap},
                             {"min_mm", clearance}});
                }
            }
        }
    }

    out.status = ok ? validation::ValidationStatus::Validated
                    : validation::ValidationStatus::Invalid;
    out.message = ok ? "Design passed all PCB checks" : "Design failed PCB checks";
    out.checks = {{"component_count", static_cast<int>(design.components.size())},
                  {"net_count", static_cast<int>(design.nets.size())},
                  {"placement_count", static_cast<int>(design.placements.size())}};
    return out;
}

}  // namespace trinity::pcb
