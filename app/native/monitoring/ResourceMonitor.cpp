#include "ResourceMonitor.hpp"

#include <chrono>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <ctime>
#include <unistd.h>
#endif

#include "../core/Time.hpp"

namespace trinity::monitoring {
namespace {

std::int64_t filetime_to_100ns(std::int64_t value) { return value; }

#if defined(_WIN32)
void get_process_cpu_times(std::int64_t& kernel_100ns, std::int64_t& user_100ns) {
    FILETIME creation{}, exit_time{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exit_time, &kernel, &user)) {
        kernel_100ns = (static_cast<std::int64_t>(kernel.dwHighDateTime) << 32) |
                       kernel.dwLowDateTime;
        user_100ns = (static_cast<std::int64_t>(user.dwHighDateTime) << 32) |
                     user.dwLowDateTime;
    } else {
        kernel_100ns = user_100ns = 0;
    }
}
#else
void get_process_cpu_times(std::int64_t& kernel_100ns, std::int64_t& user_100ns) {
    struct timespec user_ts{};
    // clockid_t (not clock_t) is what the clock_* family expects; the POSIX
    // macro CLOCK_PROCESS_CPUTIME_ID is a valid clockid_t already.
    clockid_t id = CLOCK_PROCESS_CPUTIME_ID;
    if (clock_getcpuclockid(0, &id) != 0) id = CLOCK_PROCESS_CPUTIME_ID;
    if (clock_gettime(id, &user_ts) != 0) {
        kernel_100ns = 0;
        user_100ns = 0;
        return;
    }
    user_100ns = static_cast<std::int64_t>(user_ts.tv_sec) * 10'000'000LL +
                 user_ts.tv_nsec / 100;
    kernel_100ns = 0;
}
#endif

}  // namespace

ResourceSample ResourceMonitor::sample() {
    ResourceSample sample;
    sample.timestamp_millis = core::now_millis();

#if defined(_WIN32)
    // CPU delta since last call.
    static std::int64_t last_kernel = 0, last_user = 0, last_wall_100ns = 0;

    std::int64_t kernel = 0, user = 0;
    get_process_cpu_times(kernel, user);
    FILETIME wall_now{}, wall_start_unused{};
    GetSystemTimeAsFileTime(&wall_now);
    const std::int64_t wall_100ns =
        (static_cast<std::int64_t>(wall_now.dwHighDateTime) << 32) | wall_now.dwLowDateTime;

    if (last_wall_100ns != 0) {
        const std::int64_t wall_delta = wall_100ns - last_wall_100ns;
        const std::int64_t cpu_delta = (kernel - last_kernel) + (user - last_user);
        if (wall_delta > 0) {
            // Fraction of one full core; multiply by 100 for percent-of-core.
            sample.process_cpu_percent = 100.0 * static_cast<double>(cpu_delta) /
                                         static_cast<double>(wall_delta);
        }
    }
    last_kernel = kernel;
    last_user = user;
    last_wall_100ns = wall_100ns;

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory))) {
        sample.process_working_set_bytes = memory.WorkingSetSize;
        sample.process_virtual_bytes = memory.PrivateUsage;
    }
    sample.process_thread_count = 0;  // populated via toolhelp when needed
#else
    static std::int64_t last_cpu = 0, last_wall = 0;
    std::int64_t kernel = 0, user = 0;
    get_process_cpu_times(kernel, user);
    const std::int64_t wall_now = core::monotonic_millis() * 10'000;  // to 100ns
    if (last_wall != 0) {
        const std::int64_t wall_delta = wall_now - last_wall;
        const std::int64_t cpu_delta = (kernel - last_cpu) + user - last_cpu;
        (void)cpu_delta;
        const std::int64_t cpu_total = (kernel + user) - (last_cpu);
        if (wall_delta > 0) {
            sample.process_cpu_percent =
                100.0 * static_cast<double>(cpu_total) / static_cast<double>(wall_delta);
        }
    }
    last_cpu = kernel + user;
    last_wall = wall_now;
    sample.process_working_set_bytes =
        static_cast<std::uint64_t>(::getpagesize()) * 1024;  // placeholder baseline
    sample.process_thread_count = 0;
#endif
    return sample;
}

}  // namespace trinity::monitoring
