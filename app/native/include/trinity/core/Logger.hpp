#pragma once

// Structured logging. Emits single-line JSON records so any future
// shipper (OpenTelemetry / Prometheus / Grafana) can parse them
// without changing this module's surface. Mirrors
// src/core/logging_config.py. Writes to stderr and, when configured,
// to a log file; keeps an in-memory ring buffer so the future UI can
// display recent entries.

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "Json.hpp"

namespace trinity::core {

enum class LogLevel { Debug, Info, Warning, Error };

const char* logLevelName(LogLevel level) noexcept;

class Logger {
public:
    static Logger& instance();

    void configure(LogLevel level);
    void configure(LogLevel level, const std::string& logFilePath);
    void setLogFile(const std::string& logFilePath);
    std::string logFilePath() const;
    void setMaxBuffered(size_t maxBuffered);

    void log(LogLevel level, const std::string& logger, const std::string& message,
             const Json& context = Json::object());

    void debug(const std::string& logger, const std::string& message,
               const Json& context = Json::object());
    void info(const std::string& logger, const std::string& message,
              const Json& context = Json::object());
    void warning(const std::string& logger, const std::string& message,
                 const Json& context = Json::object());
    void error(const std::string& logger, const std::string& message,
               const Json& context = Json::object());

    // Recent records (oldest first) for UI display. Thread-safe snapshot.
    std::vector<Json> recent(size_t limit = 200) const;
    void clearBuffer();

private:
    Logger() = default;

    LogLevel level_ = LogLevel::Info;
    std::string logFile_;
    size_t maxBuffered_ = 1000;
    std::deque<Json> buffer_;
    mutable std::mutex mutex_;
};

}  // namespace trinity::core
