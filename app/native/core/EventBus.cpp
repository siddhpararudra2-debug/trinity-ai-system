#include "EventBus.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>

#include "Error.hpp"
#include "Logging.hpp"

namespace trinity::core {
namespace {
ComponentLog log_("events");
}  // namespace

bool EventBus::topic_matches(const std::string& pattern, const std::string& topic) {
    if (pattern.empty()) return false;
    if (pattern == "*") return true;
    if (pattern == topic) return true;
    if (pattern.size() >= 2 && pattern.compare(pattern.size() - 2, 2, ".*") == 0) {
        // Prefix wildcard: "job.*" matches "job.progress" but not "jobs.x".
        const std::string prefix = pattern.substr(0, pattern.size() - 1);  // keeps "job."
        return topic.size() > prefix.size() && topic.compare(0, prefix.size(), prefix) == 0;
    }
    return false;
}

EventSubscriptionId EventBus::subscribe(std::string topic_pattern, Handler handler) {
    if (!handler) {
        log_.warning("ignoring subscription with no handler",
                     [&] {
                         Json ctx = Json::object();
                         ctx["topic_pattern"] = topic_pattern;
                         return ctx;
                     }());
        return 0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const EventSubscriptionId id = next_id_++;
    subscribers_.emplace(id, std::make_pair(std::move(topic_pattern), std::move(handler)));
    return id;
}

bool EventBus::unsubscribe(EventSubscriptionId id) {
    if (id == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.erase(id) > 0;
}

std::size_t EventBus::publish(const std::string& topic, Json payload) {
    // Matching handlers are copied out under the lock and invoked after it is
    // released: a handler may subscribe, unsubscribe or even publish again.
    std::vector<Handler> matched;
    Event event;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.topic = topic;
        event.payload = payload;
        event.timestamp = iso_utc_now();

        ring_.push_back(event);
        while (ring_.size() > kRingCapacity) ring_.pop_front();
        ++published_count_;

        for (const auto& entry : subscribers_) {
            if (topic_matches(entry.second.first, topic)) matched.push_back(entry.second.second);
        }
    }

    std::size_t delivered = 0;
    for (const Handler& handler : matched) {
        try {
            handler(event);
            ++delivered;
        } catch (const TrinityException& exception) {
            log_.error("event handler raised a trinity error", [&] {
                Json ctx = Json::object();
                ctx["topic"] = topic;
                ctx["error"] = exception.error().to_json();
                return ctx;
            }());
        } catch (const std::exception& exception) {
            log_.error("event handler raised an exception", [&] {
                Json ctx = Json::object();
                ctx["topic"] = topic;
                ctx["what"] = std::string(exception.what());
                return ctx;
            }());
        } catch (...) {
            log_.error("event handler raised a non-standard exception", [&] {
                Json ctx = Json::object();
                ctx["topic"] = topic;
                return ctx;
            }());
        }
    }
    return delivered;
}

std::vector<Event> EventBus::recent(std::size_t max_count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t count = std::min(max_count, ring_.size());
    return std::vector<Event>(ring_.end() - static_cast<std::ptrdiff_t>(count), ring_.end());
}

std::size_t EventBus::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscribers_.size();
}

std::size_t EventBus::published_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_count_;
}

void EventBus::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.clear();
    ring_.clear();
    published_count_ = 0;
}

}  // namespace trinity::core
