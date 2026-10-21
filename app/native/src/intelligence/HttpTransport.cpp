#include "trinity/intelligence/HttpTransport.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")
#endif

#include <stdexcept>

namespace trinity::intelligence {

namespace {

#ifdef _WIN32

std::wstring widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                          static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0) {
        throw std::runtime_error("endpoint/body are not valid UTF-8");
    }
    std::wstring wide(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        wide.data(), chars);
    return wide;
}

// Every WinHTTP handle from this call chain must be closed on every exit —
// including the early-return transport failures below.
struct CurlGuard {
    HINTERNET handle;
    explicit CurlGuard(HINTERNET h)
        : handle(h) {}
    ~CurlGuard() {
        if (handle) {
            WinHttpCloseHandle(handle);
        }
    }
};

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    int port = 443;
    bool https = true;
};

ParsedUrl crackUrl(const std::string& url) {
    const std::wstring wide = widen(url);
    URL_COMPONENTSW parts = {};
    parts.dwStructSize = sizeof(parts);
    std::wstring host(256, L'\0');
    std::wstring path(1024, L'\0');
    parts.lpszHostName = host.data();
    parts.dwHostNameLength = static_cast<DWORD>(host.size());
    parts.lpszUrlPath = path.data();
    parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &parts)) {
        throw std::runtime_error("cannot parse endpoint URL: " + url);
    }
    ParsedUrl parsed;
    parsed.host.assign(host.data(), parts.dwHostNameLength);
    parsed.path = parts.dwUrlPathLength
                      ? std::wstring(path.data(), parts.dwUrlPathLength)
                      : std::wstring(L"/");
    parsed.port = parts.nPort;
    parsed.https = (parts.nScheme == INTERNET_SCHEME_HTTPS);
    return parsed;
}

#endif  // _WIN32

}  // namespace

HttpReply DefaultHttpTransport::post(const HttpRequest& request) {
#ifndef _WIN32
    static_cast<void>(request);
    HttpReply reply;
    reply.errorText =
        "DefaultHttpTransport ships only on Windows; inject an IHttpTransport for other targets";
    return reply;
#else
    HttpReply reply;

    const ParsedUrl url = crackUrl(request.url);

    HINTERNET session = WinHttpOpen(L"Trinity/0.1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        reply.errorText = "WinHttpOpen failed (error " + std::to_string(GetLastError()) + ")";
        return reply;
    }
    CurlGuard sessionClose(session);

    HINTERNET connection =
        WinHttpConnect(session, url.host.c_str(), static_cast<INTERNET_PORT>(url.port), 0);
    if (!connection) {
        reply.errorText = "WinHttpConnect failed (error " + std::to_string(GetLastError()) + ")";
        return reply;
    }
    CurlGuard connectionClose(connection);

    HINTERNET handle = WinHttpOpenRequest(
        connection, L"POST", url.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, url.https ? WINHTTP_FLAG_SECURE : 0);
    if (!handle) {
        reply.errorText = "WinHttpOpenRequest failed (error " + std::to_string(GetLastError()) + ")";
        return reply;
    }
    CurlGuard requestClose(handle);

    const int budget = request.timeoutMs > 0 ? request.timeoutMs : 30000;
    WinHttpSetTimeouts(handle, budget, budget, budget, budget);

    // Request headers (auth values must be handled by the provider and are
    // never logged); content type is appended last so late duplicates win.
    std::wstring headers;
    for (const auto& header : request.header) {
        headers += widen(header.first) + L": " + widen(header.second) + L"\r\n";
    }
    headers += L"Content-Type: " + widen(request.contentType) + L"\r\n";

    const DWORD bodyBytes = static_cast<DWORD>(request.body.size());
    LPVOID bodyPtr = bodyBytes ? reinterpret_cast<LPVOID>(const_cast<char*>(request.body.data()))
                               : nullptr;
    const BOOL sent = WinHttpSendRequest(
        handle, headers.c_str(), static_cast<DWORD>(headers.size()), bodyPtr, bodyBytes,
        bodyBytes, 0);
    if (!sent) {
        reply.errorText = "WinHttpSendRequest failed (error " + std::to_string(GetLastError()) + ")";
        return reply;
    }
    if (!WinHttpReceiveResponse(handle, nullptr)) {
        reply.errorText = "WinHttpReceiveResponse failed (error " + std::to_string(GetLastError()) + ")";
        return reply;
    }

    DWORD statusSize = sizeof(DWORD);
    DWORD statusCode = 0;
    if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                            WINHTTP_NO_HEADER_INDEX)) {
        reply.statusCode = static_cast<long>(statusCode);
    }

    std::string body;
    DWORD available = 0;
    for (;;) {
        if (!WinHttpQueryDataAvailable(handle, &available)) {
            reply.errorText = "WinHttpQueryDataAvailable failed (error " +
                              std::to_string(GetLastError()) + ")";
            return reply;
        }
        if (available == 0) {
            break;
        }
        std::vector<char> chunk(available);
        DWORD readBytes = 0;
        if (!WinHttpReadData(handle, chunk.data(), available, &readBytes)) {
            reply.errorText = "WinHttpReadData failed (error " +
                              std::to_string(GetLastError()) + ")";
            return reply;
        }
        body.append(chunk.data(), readBytes);
        if (body.size() > (1u << 26)) {  // 64 MiB guard — no model reply is bigger
            reply.errorText = "response body exceeded the 64 MiB guard";
            return reply;
        }
    }

    reply.transportOk = true;
    reply.body = std::move(body);
    return reply;
#endif  // _WIN32
}

}  // namespace trinity::intelligence
