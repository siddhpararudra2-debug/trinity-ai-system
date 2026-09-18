#include "Logging.hpp"

#include <cstdio>
#include <ctime>

#include "Time.hpp"

namespace trinity::core {

const char* log_level_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Critical: return "CRITICAL";
    }
    return "INFO";
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::log(LogLevel level, const std::string& component, const std::string& message,
                 Json context) {
    LogRecord record;
    record.timestamp_millis = now_millis();
    record.level = level;
    record.component = component;
    record.message = message;
    record.context = std::move(context);
    emit(record);
}

void Logger::emit(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (record.level < level_) return;

    // Ring buffer always keeps the newest records for UI/crash reporting.
    ring_.push_back(record);
    while (ring_.size() > kRingCapacity) ring_.pop_front();

    // Console sink (stderr).
    std::FILE* console = stderr;
    std::fprintf(console, "%s [%s] %s: %s\n", iso_utc_from_millis(record.timestamp_millis).c_str(),
                 log_level_string(record.level), record.component.c_str(),
                 record.message.c_str());
    if (!record.context.as_object().empty()) {
        const std::string ctx = record.context.dump();
        std::fprintf(console, "    context: %s\n", ctx.c_str());
    }
    std::fflush(console);

    // File sink.
    if (file_ != nullptr) {
        auto* file = static_cast<std::FILE*>(file_);
        std::fprintf(file, "%s [%s] %s: %s", iso_utc_from_millis(record.timestamp_millis).c_str(),
                     log_level_string(record.level), record.component.c_str(),
                     record.message.c_str());
        if (!record.context.as_object().empty()) {
            const std::string ctx = record.context.dump();
            std::fprintf(file, " | %s", ctx.c_str());
        }
        std::fprintf(file, "\n");
        std::fflush(file);
    }

    // Extra sinks. Invoked without holding a re-entrant guard beyond mutex_
    // (sinks must not log synchronously to avoid deadlock).
    for (const auto& [id, sink] : sinks_) {
        (void)id;
        sink(record);
    }
}

std::vector<LogRecord> Logger::recent_records(std::size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogRecord> out;
    const std::size_t start = ring_.size() > max_count ? ring_.size() - max_count : 0;
    out.reserve(ring_.size() - start);
    for (std::size_t i = start; i < ring_.size(); ++i) out.push_back(ring_[i]);
    return out;
}

bool Logger::open_file_sink(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(static_cast<std::FILE*>(file_));
        file_ = nullptr;
    }
    std::FILE* file = nullptr;
#if defined(_WIN32)
    if (fopen_s(&file, path.c_str(), "ab") != 0) file = nullptr;
#else
    file = std::fopen(path.c_str(), "ab");
#endif
    if (file == nullptr) return false;
    file_ = file;
    return true;
}

void Logger::close_file_sink() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_ != nullptr) {
        std::fclose(static_cast<std::FILE*>(file_));
        file_ = nullptr;
    }
}

bool Logger::file_sink_active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_ != nullptr;
}

std::size_t Logger::add_sink(SinkFn sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t id = next_sink_id_++;
    sinks_.emplace_back(id, std::move(sink));
    return id;
}

void Logger::remove_sink(std::size_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it) {
        if (it->first == id) {
            sinks_.erase(it);
            return;
        }
    }
}

}  // namespace trinity::core
