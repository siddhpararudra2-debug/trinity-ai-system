#pragma once

// HTTP transport seam for model providers.
//
// Providers never touch sockets directly: they call IHttpTransport::post().
// The shipped implementation is DefaultHttpTransport (synchronous HTTPS over
// WinHTTP, Windows-only). Tests inject a fixture transport instead, which is
// what keeps contract tests offline and deterministic.

#include <functional>
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

// Receives successive response-body pieces during a streaming POST.
using BodyChunkCallback = std::function<void(const std::string& chunk)>;

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    // Synchronous POST. Providers block on this; the caller controls the
    // budget through HttpRequest::timeoutMs.
    virtual HttpReply post(const HttpRequest& request) = 0;

    // Streaming POST: onChunk receives successive body pieces as they
    // arrive (SSE deltas, chunked encodings). Default keeps the seam honest
    // for transports that only implement post(): it runs post() and hands
    // the whole body back as exactly one chunk, so legacy fixtures never
    // need to override this and providers must tolerate both shapes. Chunk
    // boundaries may split UTF-8 sequences — callers must line-buffer (the
    // SSE parser in ChatWire does).
    virtual HttpReply postStream(const HttpRequest& request,
                                 const BodyChunkCallback& onChunk);

    // Convenience overload for callers that stream only for side effects.
    HttpReply postStream(const HttpRequest& request) {
        return postStream(request, {});
    }
};

// Ships on Windows (WinHTTP). On any other platform it returns a reply with
// transportOk=false and an explanatory error — providers then answer with a
// truthful capability refusal, matching the repo's no-fake-metrics rule.
class DefaultHttpTransport : public IHttpTransport {
public:
    HttpReply post(const HttpRequest& request) override;
    HttpReply postStream(const HttpRequest& request,
                         const BodyChunkCallback& onChunk) override;
};

}  // namespace trinity::intelligence
