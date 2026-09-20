#include "trinity/core/Logger.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>

namespace trinity::core {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

const char* logLevelName(LogLevel level) noexcept {
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

void Logger::configure(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::configure(LogLevel level, const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    logFile_ = logFilePath;
}

void Logger::setLogFile(const std::string& logFilePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    logFile_ = logFilePath;
}

std::string Logger::logFilePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return logFile_;
}

void Logger::setMaxBuffered(size_t maxBuffered) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxBuffered_ = maxBuffered;
    while (buffer_.size() > maxBuffered_) {
        buffer_.pop_front();
    }
}

void Logger::log(LogLevel level, const std::string& logger, const std::string& message,
                 const Json& context) {
    Json record;
    std::string logFile;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(level) < static_cast<int>(level_)) {
            return;
        }
        const auto now = std::chrono::system_clock::now();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch())
                .count();
        record = {{"ts", ms},
                  {"level", logLevelName(level)},
                  {"logger", logger},
                  {"message", message}};
        if (!context.is_null() && !context.empty()) {
            record["ctx"] = context;
        }
        buffer_.push_back(record);
        while (buffer_.size() > maxBuffered_) {
            buffer_.pop_front();
        }
        logFile = logFile_;
    }
    std::string line = record.dump();
    line += "\n";
    std::fputs(line.c_str(), stderr);
    std::fflush(stderr);
    if (!logFile.empty()) {
        try {
            std::error_code ec;
            std::filesystem::create_directories(
                std::filesystem::path(logFile).parent_path(), ec);
            FILE* file = nullptr;
#ifdef _WIN32
            fopen_s(&file, logFile.c_str(), "a");
#else
            file = std::fopen(logFile.c_str(), "a");
#endif
            if (file != nullptr) {
                std::fputs(line.c_str(), file);
                std::fclose(file);
            }
        } catch (...) {
            // Logging must never throw.
        }
    }
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

std::vector<Json> Logger::recent(size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Json> out;
    const size_t count = (std::min)(limit, buffer_.size());
    out.reserve(count);
    auto it = buffer_.end();
    for (size_t i = 0; i < count; ++i) {
        --it;
        out.push_back(*it);
    }
    // Return oldest-first for UI display.
    std::reverse(out.begin(), out.end());
    return out;
}

void Logger::clearBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
}

}  // namespace trinity::core
