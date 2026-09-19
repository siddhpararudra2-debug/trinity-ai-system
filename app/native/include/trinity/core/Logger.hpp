#pragma once

// Structured logging. Emits single-line JSON records so any future
// shipper (OpenTelemetry / Prometheus / Grafana) can parse them
// without changing this module's surface. Mirrors
// src/core/logging_config.py.

#include <mutex>
#include <string>

#include "Json.hpp"

namespace trinity::core {

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    static Logger& instance();

    void configure(LogLevel level);
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

private:
    Logger() = default;

    LogLevel level_ = LogLevel::Info;
    std::mutex mutex_;
};

}  // namespace trinity::core
