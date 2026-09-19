#include "trinity/core/Logger.hpp"

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace trinity::core {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::configure(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

namespace {

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

}  // namespace

void Logger::log(LogLevel level, const std::string& logger, const std::string& message,
                 const Json& context) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
            .count();
    Json record = {{"ts", ms},
                   {"level", levelName(level)},
                   {"logger", logger},
                   {"message", message}};
    if (!context.is_null() && !context.empty()) {
        record["ctx"] = context;
    }
    std::string line = record.dump();
    line += "\n";
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
}

void Logger::debug(const std::string& logger, const std::string& message,
                   const Json& context) {
    log(LogLevel::Debug, logger, message, context);
}

void Logger::info(const std::string& logger, const std::string& message,
                  const Json& context) {
    log(LogLevel::Info, logger, message, context);
}

void Logger::warning(const std::string& logger, const std::string& message,
                     const Json& context) {
    log(LogLevel::Warning, logger, message, context);
}

void Logger::error(const std::string& logger, const std::string& message,
                   const Json& context) {
    log(LogLevel::Error, logger, message, context);
}

}  // namespace trinity::core
