#pragma once

// Deterministic requirement parser: converts natural-language engineering
// requests into validated structured Intents without any LLM. Additional
// request types are added as new try* handlers; the parse() dispatcher
// itself never needs editing beyond registration order.
//
// Canonical units (internal representation):
//   length -> mm, angle -> deg, mass -> g, force -> N, pressure -> Pa.
// The original value/unit strings are always preserved in
// Intent::rawMetadata for traceability.

#include <string>
#include <vector>

#include "../core/Json.hpp"
#include "Intent.hpp"

namespace trinity::intelligence {

/// Unit normalization result.
struct NormalizedQuantity {
    bool ok = false;
    double normalizedValue = 0.0;
    std::string canonicalUnit;
    std::string category;  // "length" | "angle" | "mass" | "force" | "pressure"
};

class UnitNormalizer {
public:
    /// Normalize a value+unit into canonical form. Returns ok=false for
    /// unknown units. Unit matching is case-insensitive.
    static NormalizedQuantity normalize(double value, const std::string& unit);
    static bool isSupportedUnit(const std::string& unit);
    static std::string canonicalUnitFor(const std::string& unit);
};

struct ParseResult {
    Intent intent;
    ParseStatus status = ParseStatus::Invalid;
    std::vector<std::string> errors;

    core::Json toJson() const;
};

class RequirementParser {
public:
    RequirementParser() = default;

    /// Parse a natural-language request. Never throws; failures are
    /// reported as ParseStatus::Invalid with errors populated. Never
    /// invents missing values.
    ParseResult parse(const std::string& text) const;

private:
    ParseResult tryExplicitEngine(const std::string& text,
                                  const std::string& lowered) const;
    ParseResult tryCadRequest(const std::string& text,
                              const std::string& lowered,
                              const std::string& forcedDomain) const;
    ParseResult tryMathRequest(const std::string& text,
                               const std::string& lowered,
                               const std::string& forcedDomain) const;
    ParseResult tryPcbRequest(const std::string& text,
                              const std::string& lowered,
                              const std::string& forcedDomain) const;
    ParseResult tryFirmwareRequest(const std::string& text,
                                   const std::string& lowered,
                                   const std::string& forcedDomain) const;
    ParseResult tryVisionRequest(const std::string& text,
                                 const std::string& lowered,
                                 const std::string& forcedDomain) const;
};

}  // namespace trinity::intelligence
