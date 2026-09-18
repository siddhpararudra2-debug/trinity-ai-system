// Trinity — UUID v4 generation (random, no MAC leakage), lowercase form,
// identical to Python's str(uuid.uuid4()).
#pragma once

#include <string>
#include <string_view>

namespace trinity::core {

// Returns a fresh random UUID v4, e.g. "3f2a9c1e-8b7d-4c2a-9e1f-0d6b5a4c3e2d".
std::string new_uuid();

// True if the string is a well-formed UUID (8-4-4-4-12 hex, dashes).
bool is_valid_uuid(std::string_view text);

}  // namespace trinity::core
