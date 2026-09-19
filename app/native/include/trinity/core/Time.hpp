#pragma once

// UTC timestamps in ISO-8601 form, matching the Python backend's
// datetime.now(UTC).isoformat() representation.

#include <string>

namespace trinity::core {

std::string utcNowIso();

}  // namespace trinity::core
