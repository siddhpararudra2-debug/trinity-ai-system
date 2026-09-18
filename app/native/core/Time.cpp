#include "Time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace trinity::core {

std::int64_t now_millis() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
        .count();
}

std::string iso_utc_from_millis(std::int64_t millis) {
    const std::time_t seconds = static_cast<std::time_t>(millis / 1000);
    const int ms = static_cast<int>(millis % 1000);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                  utc.tm_sec, ms);
    return std::string(buf);
}

std::string iso_utc_now() { return iso_utc_from_millis(now_millis()); }

std::int64_t monotonic_millis() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
        .count();
}

}  // namespace trinity::core
