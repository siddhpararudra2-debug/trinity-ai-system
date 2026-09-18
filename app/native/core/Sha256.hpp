// Trinity — SHA-256 (FIPS 180-4), dependency-free.
// Used for artifact content addressing and cache keys, matching the V1
// Python backend's hashlib.sha256 usage.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace trinity::core {

class Sha256 {
public:
    Sha256() { reset(); }

    void reset();
    void update(const void* data, std::size_t length);
    std::string finish_hex();  // lowercase 64-char hex digest

private:
    void process_block(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint64_t bit_length_;
    std::uint8_t buffer_[64];
    std::size_t buffer_length_;
};

// Convenience: hash a contiguous byte range in one call.
std::string sha256_hex(std::string_view data);

}  // namespace trinity::core
