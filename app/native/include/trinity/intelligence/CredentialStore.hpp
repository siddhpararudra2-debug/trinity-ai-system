#pragma once

#include <string>
#include <optional>
#include "../core/Result.hpp"

#include <unordered_map>

namespace trinity::intelligence {

class CredentialStore {
public:
    static CredentialStore& instance();

    void setCredential(const std::string& providerId, const std::string& key);
    std::optional<std::string> getCredential(const std::string& providerId) const;
    void clearCredential(const std::string& providerId);

private:
    CredentialStore() = default;
    std::unordered_map<std::string, std::string> _memoryStore;
};

} // namespace trinity::intelligence
