#include "trinity/intelligence/CredentialStore.hpp"
#include <cstdlib>

namespace trinity::intelligence {

CredentialStore& CredentialStore::instance() {
    static CredentialStore inst;
    return inst;
}

void CredentialStore::setCredential(const std::string& providerId, const std::string& key) {
    // In a real application, use Windows DPAPI or Credential Manager.
    // For now, we fallback to env vars or just keep it in memory for the session.
    // NOTE: In production, NEVER store this in memory unencrypted.
    _memoryStore[providerId] = key;
}

std::optional<std::string> CredentialStore::getCredential(const std::string& providerId) const {
    auto it = _memoryStore.find(providerId);
    if (it != _memoryStore.end()) {
        return it->second;
    }
    
    // Environment variable fallback
    std::string envName = providerId;
    for (char& c : envName) {
        c = std::toupper(c);
    }
    envName += "_API_KEY";
    
    const char* env = std::getenv(envName.c_str());
    if (env != nullptr) {
        return std::string(env);
    }
    
    return std::nullopt;
}

void CredentialStore::clearCredential(const std::string& providerId) {
    _memoryStore.erase(providerId);
}

} // namespace trinity::intelligence
