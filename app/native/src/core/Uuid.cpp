#include "trinity/core/Uuid.hpp"

#include <iomanip>
#include <random>
#include <sstream>

namespace trinity::core {

std::string newUuid() {
    std::random_device device;
    std::mt19937_64 rng(device());
    std::uniform_int_distribution<unsigned long long> dist;

    const unsigned long long hi = dist(rng);
    const unsigned long long lo = dist(rng);

    // Version 4 (random) with RFC 4122 variant bits.
    const unsigned long long timeHi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    const unsigned long long clockSeq = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << ((hi >> 32) & 0xFFFFFFFFULL) << "-"
        << std::setw(4) << ((hi >> 16) & 0xFFFFULL) << "-" << std::setw(4)
        << (timeHi & 0xFFFFULL) << "-" << std::setw(4) << ((clockSeq >> 48) & 0xFFFFULL)
        << "-" << std::setw(12) << (clockSeq & 0xFFFFFFFFFFFFULL);
    return out.str();
}

}  // namespace trinity::core
