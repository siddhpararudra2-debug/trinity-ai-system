#include "trinity/artifacts/Checksum.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <bcrypt.h>

#ifndef BCRYPT_SHA256_ALGORITHM_NAME
#define BCRYPT_SHA256_ALGORITHM_NAME L"SHA256"
#endif
#endif

namespace trinity::artifacts {

std::string sha256File(const std::string& path) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM_NAME, nullptr, 0) != 0) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    }
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength),
                          sizeof(hashLength), &resultLength, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptGetProperty failed");
    }
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptCreateHash failed");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("Cannot open file for checksum: " + path);
    }
    std::vector<unsigned char> buffer(1 << 20);
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0 &&
            BCryptHashData(hash, buffer.data(), static_cast<ULONG>(count), 0) != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCryptHashData failed");
        }
    }
    std::vector<unsigned char> digest(hashLength);
    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCryptFinishHash failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream out;
    out << std::hex;
    for (unsigned char byte : digest) {
        out.width(2);
        out.fill('0');
        out << static_cast<int>(byte);
    }
    return out.str();
#else
    throw std::runtime_error("SHA-256 checksum is only implemented on Windows: " + path);
#endif
}

}  // namespace trinity::artifacts
