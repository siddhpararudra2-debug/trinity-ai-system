#include "trinity/core/Uuid.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>
#endif

namespace trinity::core {

namespace {

bool fillRandom(unsigned char* data, size_t size) {
#ifdef _WIN32
    // Prefer OS CSPRNG; fall back to std::random_device on failure.
    NTSTATUS status = BCryptGenRandom(nullptr, data, static_cast<ULONG>(size),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status >= 0) {
        return true;
    }
#endif
    try {
        std::random_device device;
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<unsigned char>(device() & 0xFFU);
        }
        return true;
    } catch (...) {
        // Last resort: seeded PRNG (still sets v4/variant bits correctly).
        std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<unsigned long long> dist;
        size_t i = 0;
        while (i < size) {
            unsigned long long v = dist(rng);
            for (int b = 0; b < 8 && i < size; ++b, ++i) {
                data[i] = static_cast<unsigned char>((v >> (b * 8)) & 0xFFU);
            }
        }
        return true;
    }
}

}  // namespace

std::string newUuid() {
    std::array<unsigned char, 16> bytes{};
    fillRandom(bytes.data(), bytes.size());

    // Version 4 + RFC 4122 variant.
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        out << std::setw(2) << static_cast<int>(bytes[static_cast<size_t>(i)]);
        if (i == 3 || i == 5 || i == 7 || i == 9) {
            out << "-";
        }
    }
    return out.str();
}

bool isValidUuid(const std::string& value) noexcept {
    if (value.size() != 36) {
        return false;
    }
    for (size_t i = 0; i < value.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (value[i] != '-') {
                return false;
            }
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    // Version nibble must be 1-8 (we generate 4); variant must be 8/9/a/b.
    const char version = static_cast<char>(std::tolower(static_cast<unsigned char>(value[14])));
    const char variant = static_cast<char>(std::tolower(static_cast<unsigned char>(value[19])));
    if (version < '1' || version > '8') {
        return false;
    }
    if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b') {
        return false;
    }
    return true;
}

}  // namespace trinity::core
