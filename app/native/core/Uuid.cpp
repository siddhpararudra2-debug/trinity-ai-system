#include "Uuid.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>

namespace trinity::core {
namespace {

std::uint64_t monotonic_seed() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// One process-wide PRNG. std::random_device is non-deterministic on MSVC and
// MinGW-w64; seeding an mt19937_64 once from device + monotonic clock is the
// portable middle ground. All use is mutex-guarded for thread safety.
std::mt19937_64& rng() {
    static std::mt19937_64 engine = [] {
        std::random_device device;
        const std::uint64_t seed =
            (static_cast<std::uint64_t>(device()) << 32) ^ device() ^ monotonic_seed();
        return std::mt19937_64(seed);
    }();
    return engine;
}

std::mutex& rng_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

std::string new_uuid() {
    std::uint64_t a = 0;
    std::uint64_t b = 0;
    {
        std::lock_guard<std::mutex> lock(rng_mutex());
        std::mt19937_64& engine = rng();
        a = engine();
        b = engine();
    }

    unsigned char bytes[16];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<unsigned char>((a >> (i * 8)) & 0xFF);
        bytes[i + 8] = static_cast<unsigned char>((b >> (i * 8)) & 0xFF);
    }
    // Version 4 and variant bits per RFC 4122.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex[(bytes[i] >> 4) & 0xF]);
        out.push_back(hex[bytes[i] & 0xF]);
    }
    return out;
}

bool is_valid_uuid(std::string_view text) {
    if (text.size() != 36) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace trinity::core
