#include "trinity/pcb/Validators.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

/// Shortest distance between two closed segments (a1→a2, b1→b2), mm.
/// Public (also in Validators.hpp) so doctests exercise the same
/// routine the trace clearance evaluator uses.
double trackSegmentDistanceMm(double ax1, double ay1, double ax2, double ay2,
                              double bx1, double by1, double bx2, double by2) {
    auto pointSeg = [](double px, double py, double sx, double sy, double ex,
                       double ey, double& best2) {
        const double dx = ex - sx, dy = ey - sy;
        const double len2 = dx * dx + dy * dy;
        double t = (len2 <= 0.0) ? 0.0 : ((px - sx) * dx + (py - sy) * dy) / len2;
        t = std::clamp(t, 0.0, 1.0);
        const double cx = sx + t * dx, cy = sy + t * dy;
        best2 = std::min(best2, (px - cx) * (px - cx) + (py - cy) * (py - cy));
    };
    double best2 = std::numeric_limits<double>::infinity();
    pointSeg(bx1, by1, ax1, ay1, ax2, ay2, best2);
    pointSeg(bx2, by2, ax1, ay1, ax2, ay2, best2);
    pointSeg(ax1, ay1, bx1, by1, bx2, by2, best2);
    pointSeg(ax2, ay2, bx1, by1, bx2, by2, best2);
    return std::sqrt(best2);
}

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

    // --- Traces: geometry, nets, widths, clearance, keepout ---
    if (!design.tracks.empty()) {
        const double ruleClearance =
            design.rules.empty() ? 0.2 : design.rules.front().minClearanceMm;
        const double minTrack =
            design.rules.empty() ? 0.2 : design.rules.front().minTrackMm;
        const double ox = design.board.originMm.x;
        const double oy = design.board.originMm.y;
        for (const auto& track : design.tracks) {
            const std::string label =
                "'" + track.net + "' (" + std::to_string(track.x1Mm) + "," +
                std::to_string(track.y1Mm) + ")-(" +
                std::to_string(track.x2Mm) + "," + std::to_string(track.y2Mm) + ")";
            if (!track.net.empty() && design.findNet(track.net) == nullptr) {
                ok = false;
                addRule(out, "trace.net_known", validation::Severity::Error,
                        false, "Track references unknown net '" + track.net + "'",
                        {{"net", track.net}});
            }
            if (!std::isfinite(track.widthMm) || track.widthMm <= 0.0 ||
                !std::isfinite(track.x1Mm) || !std::isfinite(track.y1Mm) ||
                !std::isfinite(track.x2Mm) || !std::isfinite(track.y2Mm)) {
                ok = false;
                addRule(out, "trace.geometry", validation::Severity::Error,
                        false, "Track " + label + " has non-finite geometry", {});
                continue;
            }
            if (track.widthMm < minTrack - 1e-9) {
                ok = false;
                addRule(out, "trace.width", validation::Severity::Error, false,
                        "Track " + label + " width " +
                            std::to_string(track.widthMm) +
                            " mm is below the rule minimum",
                        {{"width_mm", track.widthMm}, {"min_mm", minTrack}});
            }
            const double minX =
                std::min(track.x1Mm, track.x2Mm) - track.widthMm / 2.0;
            const double minY =
                std::min(track.y1Mm, track.y2Mm) - track.widthMm / 2.0;
            const double maxX =
                std::max(track.x1Mm, track.x2Mm) + track.widthMm / 2.0;
            const double maxY =
                std::max(track.y1Mm, track.y2Mm) + track.widthMm / 2.0;
            if (minX < ox - 1e-9 || minY < oy - 1e-9 ||
                maxX > ox + design.board.widthMm + 1e-9 ||
                maxY > oy + design.board.heightMm + 1e-9) {
                ok = false;
                addRule(out, "trace.in_bounds", validation::Severity::Error,
                        false, "Track " + label + " leaves the board", {});
            }
        }
        for (size_t i = 0; i < design.tracks.size(); ++i) {
            for (size_t j = i + 1; j < design.tracks.size(); ++j) {
                const TrackSegment& a = design.tracks[i];
                const TrackSegment& b = design.tracks[j];
                if (a.net == b.net) {
                    continue;  // same net may touch (series segments)
                }
                const double gap =
                    trackSegmentDistanceMm(a.x1Mm, a.y1Mm, a.x2Mm, a.y2Mm,
                                           b.x1Mm, b.y1Mm, b.x2Mm, b.y2Mm) -
                    (a.widthMm + b.widthMm) / 2.0;
                if (gap < ruleClearance) {
                    ok = false;
                    addRule(out, "trace.clearance",
                            validation::Severity::Error, false,
                            "Clearance between different-net tracks ('" +
                                a.net + "' and '" + b.net +
                                "') is below minimum",
                            {{"a", a.net},
                             {"b", b.net},
                             {"gap_mm", std::max(0.0, gap)},
                             {"min_mm", ruleClearance}});
                }
            }
        }
        // Tracks must not cut through foreign component bodies
        // (distance segment↔courtyard box > 0 ⇒ pass; endpoint inside
        // or the swept width touching the box ⇒ fail).
        for (const auto& track : design.tracks) {
            for (const auto& place : design.placements) {
                const Component* comp = design.findComponent(place.ref);
                if (comp == nullptr) continue;
                if (comp->pins.empty()) continue;
                bool foreign = true;
                for (const auto& pin : comp->pins) {
                    if (pin.net == track.net) foreign = false;
                }
                if (!foreign) continue;
                double x0, y0, x1, y1;
                if (!placedBox(design, place, x0, y0, x1, y1)) continue;
                const bool endpointInside =
                    (track.x1Mm >= x0 && track.x1Mm <= x1 &&
                     track.y1Mm >= y0 && track.y1Mm <= y1) ||
                    (track.x2Mm >= x0 && track.x2Mm <= x1 &&
                     track.y2Mm >= y0 && track.y2Mm <= y1);
                double edge = std::numeric_limits<double>::infinity();
                if (!endpointInside) {
                    edge = std::min(
                        std::min(trackSegmentDistanceMm(
                                     track.x1Mm, track.y1Mm, track.x2Mm,
                                     track.y2Mm, x0, y0, x1, y0),
                                 trackSegmentDistanceMm(
                                     track.x1Mm, track.y1Mm, track.x2Mm,
                                     track.y2Mm, x1, y0, x1, y1)),
                        std::min(trackSegmentDistanceMm(
                                     track.x1Mm, track.y1Mm, track.x2Mm,
                                     track.y2Mm, x1, y1, x0, y1),
                                 trackSegmentDistanceMm(
                                     track.x1Mm, track.y1Mm, track.x2Mm,
                                     track.y2Mm, x0, y1, x0, y0)));
                }
                const double swept = edge - track.widthMm / 2.0;
                if (endpointInside || swept <= 0.0) {
                    ok = false;
                    addRule(out, "trace.keepout", validation::Severity::Error,
                            false,
                            "Track net '" + track.net +
                                "' crosses component '" + place.ref + "'",
                            {{"net", track.net}, {"ref", place.ref}});
                }
            }
        }
        addRule(out, "trace.checked", validation::Severity::Info, true,
                "Trace DRC executed on " +
                    std::to_string(design.tracks.size()) + " segments");
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
