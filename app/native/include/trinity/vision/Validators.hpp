#pragma once

// Vision validation rules — mirrors firmware/Validators.hpp.
// Only VALIDATED / INVALID are ever returned; VERIFIED requires an
// independent ground-truth check that does not exist in V1.

#include "../validation/ValidationResult.hpp"
#include "VisionProject.hpp"

namespace trinity::vision {

// Validate a completed ImageResult / VisionProject. Returns Validated
// when required output checks pass, Invalid otherwise. Never Verified.
validation::ValidationResult validateVisionResult(const ImageResult& result,
                                                  const std::string& operation);

validation::ValidationResult validateVisionProject(const VisionProject& project);

}  // namespace trinity::vision
