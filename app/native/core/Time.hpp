// Trinity — time utilities. All persisted timestamps are ISO-8601 UTC,
// matching the V1 Python backend's datetime.now(timezone.utc).isoformat().
#pragma once

#include <cstdint>
#include <string>

namespace trinity::core {

// Milliseconds since the Unix epoch (system clock).
std::int64_t now_millis();

// "YYYY-MM-DDTHH:MM:SS.mmmZ" — drop-in compatible with V1 JSON timestamps.
std::string iso_utc_now();
std::string iso_utc_from_millis(std::int64_t millis);

// Monotonic clock for durations (never persisted).
std::int64_t monotonic_millis();

}  // namespace trinity::core
