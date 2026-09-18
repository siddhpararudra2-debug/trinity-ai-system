// Trinity — hardware/resource monitoring (brief #29).
// Samples this process and the system (CPU %, working set, thread count)
// using pure Win32 on Windows and stdlib fallbacks elsewhere. No external
// dependencies; sampling is cheap enough to run from a UI timer.
#pragma once

#include <cstdint>

namespace trinity::monitoring {

struct ResourceSample {
    double process_cpu_percent = 0.0;   // since previous sample
    std::uint64_t process_working_set_bytes = 0;
    std::uint32_t process_thread_count = 0;
    std::uint64_t process_virtual_bytes = 0;
    std::int64_t timestamp_millis = 0;
};

class ResourceMonitor {
public:
    // Samples current process metrics. The first call establishes the CPU
    // baseline (cpu_percent will read 0.0).
    ResourceSample sample();
};

}  // namespace trinity::monitoring
