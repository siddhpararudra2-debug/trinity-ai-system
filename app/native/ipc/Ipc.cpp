#include "Ipc.hpp"

#include "../core/Uuid.hpp"

namespace trinity::ipc {

std::string Frame::encode() const {
    core::Json out = core::Json::object();
    out["id"] = id.empty() ? core::new_uuid() : id;
    out["type"] = type;
    if (type == "request") {
        out["method"] = method;
        out["payload"] = payload;
    } else if (type == "event") {
        out["name"] = name;
        out["payload"] = payload;
    } else {
        out["ok"] = ok;
        if (ok) {
            out["result"] = payload;
        } else {
            out["error"] = error.to_json();
        }
    }
    // Frame = single line of JSON terminated by '\n' (length prefix handled
    // by the stream layer; newline framing is the on-wire truth here).
    return out.dump() + "\n";
}

core::Result<Frame> Frame::decode(const std::string& line) {
    if (line.empty()) {
        return core::Result<Frame>::fail(
            core::Error(core::ErrorCode::IpcProtocolError, "empty frame"));
    }
    core::Json parsed;
    try {
        parsed = core::Json::parse(line);
    } catch (const std::exception& exception) {
        return core::Result<Frame>::fail(
            core::Error(core::ErrorCode::IpcProtocolError, exception.what()));
    }
    if (!parsed.is_object()) {
        return core::Result<Frame>::fail(
            core::Error(core::ErrorCode::IpcProtocolError, "frame must be an object"));
    }
    Frame frame;
    if (const core::Json* id = parsed.find("id")) frame.id = id->as_string();
    if (const core::Json* type = parsed.find("type")) frame.type = type->as_string();
    if (frame.type != "request" && frame.type != "response" && frame.type != "event") {
        return core::Result<Frame>::fail(core::Error(core::ErrorCode::IpcProtocolError,
                                                     "unknown frame type '" + frame.type + "'"));
    }
    if (const core::Json* method = parsed.find("method")) frame.method = method->as_string();
    if (const core::Json* name = parsed.find("name")) frame.name = name->as_string();
    if (const core::Json* payload = parsed.find("payload")) frame.payload = *payload;
    if (const core::Json* result = parsed.find("result")) frame.payload = *result;
    if (const core::Json* ok = parsed.find("ok")) frame.ok = ok->as_bool();
    if (const core::Json* error = parsed.find("error")) frame.error = core::Error::from_json(*error);
    return core::Result<Frame>::ok(std::move(frame));
}

void IpcServer::register_method(const std::string& method, IpcHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[method] = std::move(handler);
}

void IpcServer::register_event_listener(
    const std::string& name, std::function<void(const core::Json&)> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_[name].push_back(std::move(listener));
}

core::Result<std::string> IpcServer::handle_line(const std::string& line) {
    auto frame_result = Frame::decode(line);
    if (frame_result.is_error()) {
        // Protocol-level error response referencing the raw line.
        Frame response;
        response.type = "response";
        response.ok = false;
        response.error = frame_result.error();
        return core::Result<std::string>::ok(response.encode());
    }
    const Frame& request = frame_result.value();
    if (request.type != "request") {
        Frame response;
        response.id = request.id;
        response.type = "response";
        response.ok = false;
        response.error = core::Error(core::ErrorCode::IpcProtocolError,
                                     "expected a request frame");
        return core::Result<std::string>::ok(response.encode());
    }

    Frame response;
    response.id = request.id;
    response.type = "response";

    IpcHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(request.method);
        if (it == handlers_.end()) {
            response.ok = false;
            response.error = core::Error(core::ErrorCode::RequestValidationError,
                                         "no such method '" + request.method + "'");
            return core::Result<std::string>::ok(response.encode());
        }
        handler = it->second;
    }

    try {
        response.payload = handler(request.payload);
        response.ok = true;
    } catch (const core::TrinityException& exception) {
        response.ok = false;
        response.error = exception.error();
    } catch (const std::exception& exception) {
        response.ok = false;
        response.error = core::Error(core::ErrorCode::EngineExecutionError, exception.what());
    }
    return core::Result<std::string>::ok(response.encode());
}

void IpcServer::emit_event(const std::string& name, const core::Json& payload) {
    std::vector<std::function<void(const core::Json&)>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = listeners_.find(name);
        if (it == listeners_.end()) return;
        targets = it->second;
    }
    for (const auto& listener : targets) listener(payload);
}

core::Result<core::Json> IpcServer::call(const std::string& method, const core::Json& payload) {
    core::Json request = core::Json::object();
    request["method"] = method;
    request["payload"] = payload;
    core::Json frame = core::Json::object();
    frame["id"] = core::new_uuid();
    frame["type"] = "request";
    frame["method"] = method;
    frame["payload"] = payload;

    auto line = handle_line(frame.dump());
    if (line.is_error()) return core::Result<core::Json>::fail(line.take_error());
    std::string trimmed = line.value();
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    auto response = Frame::decode(trimmed);
    if (response.is_error()) return core::Result<core::Json>::fail(response.take_error());
    if (!response.value().ok) return core::Result<core::Json>::fail(response.value().error);
    return core::Result<core::Json>::ok(response.value().payload);
}

void LineSplitter::feed(const char* data, std::size_t length,
                        std::vector<std::string>& out_lines) {
    buffer_.append(data, length);
    std::size_t start = 0;
    while (true) {
        const std::size_t newline = buffer_.find('\n', start);
        if (newline == std::string::npos) break;
        std::string line = buffer_.substr(start, newline - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) out_lines.push_back(std::move(line));
        start = newline + 1;
    }
    if (start > 0) buffer_.erase(0, start);
}

}  // namespace trinity::ipc
