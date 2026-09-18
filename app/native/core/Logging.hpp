// Trinity — structured logging with level filtering, in-memory ring buffer
// and optional file sink. Mirrors the intent of the V1 Python logging config:
// every record carries a timestamp, level, component ("logger") and message,
// plus optional structured context fields.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "../core/Json.hpp"

namespace trinity::core {

enum class LogLevel { Trace = 0, Debug = 1, Info = 2, Warning = 3, Error = 4, Critical = 5 };

const char* log_level_string(LogLevel level);

struct LogRecord {
    std::int64_t timestamp_millis = 0;
    LogLevel level = LogLevel::Info;
    std::string component;
    std::string message;
    Json context = Json::object();
};

// Thread-safe logger. Sinks receive records that pass the level threshold:
//  - console sink (stderr) — always available
//  - file sink — enabled with open_file_sink()
//  - ring buffer — last N records, for the UI log panel and crash reports
class Logger {
public:
    using SinkFn = std::function<void(const LogRecord&)>;

    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;

    void log(LogLevel level, const std::string& component, const std::string& message,
             Json context = Json::object());

    // Ring buffer access (thread-safe copy).
    std::vector<LogRecord> recent_records(std::size_t max_count) const;
    std::size_t ring_capacity() const { return kRingCapacity; }

    // File sink. Returns false when the file cannot be opened (logging must
    // never take the application down).
    bool open_file_sink(const std::string& path);
    void close_file_sink();
    bool file_sink_active() const;

    // Extra sinks (e.g. IPC log streaming). The returned id can remove_sink()d.
    std::size_t add_sink(SinkFn sink);
    void remove_sink(std::size_t id);

private:
    static constexpr std::size_t kRingCapacity = 2048;

    Logger() = default;

    void emit(const LogRecord& record);

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::deque<LogRecord> ring_;
    void* file_ = nullptr;  // FILE* hidden to avoid <cstdio> in the header
    std::size_t next_sink_id_ = 1;
    std::vector<std::pair<std::size_t, SinkFn>> sinks_;
};

// Convenience facade: components hold one Logger each, e.g. Logger log{"jobs"}.
class ComponentLog {
public:
    explicit ComponentLog(std::string component) : component_(std::move(component)) {}

    void trace(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Trace, component_, message, std::move(context));
    }
    void debug(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Debug, component_, message, std::move(context));
    }
    void info(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Info, component_, message, std::move(context));
    }
    void warning(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Warning, component_, message, std::move(context));
    }
    void error(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Error, component_, message, std::move(context));
    }
    void critical(const std::string& message, Json context = Json::object()) const {
        Logger::instance().log(LogLevel::Critical, component_, message, std::move(context));
    }

private:
    std::string component_;
};

}  // namespace trinity::core
