#pragma once

// HTTP transport seam for model providers.
//
// Providers never touch sockets directly: they call IHttpTransport::post().
// The shipped implementation is DefaultHttpTransport (synchronous HTTPS over
// WinHTTP, Windows-only). Tests inject a fixture transport instead, which is
// what keeps contract tests offline and deterministic.

#include <string>
#include <utility>
#include <vector>

namespace trinity::intelligence {

struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> header;  // name,value
    std::string contentType = "application/json";
    std::string body;                  // UTF-8 encoded payload
    int timeoutMs = 30000;             // applied to resolve/connect/send/receive
};

struct HttpReply {
    bool transportOk = false;  // false = failed before any HTTP status existed
    long statusCode = 0;
    std::string body;      // raw response body, UTF-8 as received
    std::string errorText; // transport diagnostics when !transportOk
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    // Synchronous POST. Providers block on this; the caller controls the
    // budget through HttpRequest::timeoutMs.
    virtual HttpReply post(const HttpRequest& request) = 0;
};

// Ships on Windows (WinHTTP). On any other platform it returns a reply with
// transportOk=false and an explanatory error — providers then answer with a
// truthful capability refusal, matching the repo's no-fake-metrics rule.
class DefaultHttpTransport : public IHttpTransport {
public:
    HttpReply post(const HttpRequest& request) override;
};

}  // namespace trinity::intelligence
