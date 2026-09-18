// Trinity — event bus (brief §Event bus, §structured JSON internal messages).
//
// One process-wide-style hub that every subsystem can publish into without
// knowing who consumes. It is the seam that keeps the UI off the worker
// threads: engines and jobs publish, the desktop shell subscribes and marshals
// to the Qt event loop on its own terms.
//
// Topic matching (deliberately tiny, no regex):
//   "*"            every event
//   "job.*"        every event whose topic starts with "job."
//   "job.finished" exact match
//
// Delivery is synchronous on the publishing thread; handlers must be cheap and
// must never block. A handler that throws is logged and skipped — a broken
// consumer never takes down the publisher.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "Json.hpp"
#include "Time.hpp"

namespace trinity::core {

using EventSubscriptionId = std::uint64_t;

struct Event {
    std::string topic;
    Json payload = Json::object();
    std::string timestamp;  // ISO-8601 UTC

    Json to_json() const {
        Json out = Json::object();
        out["topic"] = topic;
        out["payload"] = payload;
        out["timestamp"] = timestamp;
        return out;
    }
};

// Well-known topics. Publishes and subscriptions share these constants so a
// typo cannot silently orphan a subscriber.
namespace topics {
inline constexpr const char* kApplicationStarted = "app.started";
inline constexpr const char* kApplicationStopped = "app.stopped";
inline constexpr const char* kProjectChanged = "project.changed";
inline constexpr const char* kJobQueued = "job.queued";
inline constexpr const char* kJobProgress = "job.progress";
inline constexpr const char* kJobLog = "job.log";
inline constexpr const char* kJobFinished = "job.finished";
inline constexpr const char* kArtifactStored = "artifact.stored";
inline constexpr const char* kValidationRecorded = "validation.recorded";
inline constexpr const char* kPluginChanged = "plugin.changed";
inline constexpr const char* kCommandExecuted = "command.executed";
inline constexpr const char* kLogRecord = "log.record";
}  // namespace topics

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    EventBus() = default;
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // True when `topic` satisfies `pattern` (see the matching rules above).
    static bool topic_matches(const std::string& pattern, const std::string& topic);

    // Registers a handler. Returns an id usable with unsubscribe().
    EventSubscriptionId subscribe(std::string topic_pattern, Handler handler);

    // Removes a subscription. Returns false when the id is unknown.
    bool unsubscribe(EventSubscriptionId id);

    // Publishes an event to every matching subscriber. Returns the number of
    // handlers that completed without throwing.
    std::size_t publish(const std::string& topic, Json payload = Json::object());

    // Most recent events (oldest first), for UI replay after a reconnect.
    std::vector<Event> recent(std::size_t max_count) const;

    std::size_t subscriber_count() const;
    std::size_t published_count() const;
    void clear();

private:
    static constexpr std::size_t kRingCapacity = 512;

    mutable std::mutex mutex_;
    std::map<EventSubscriptionId, std::pair<std::string, Handler>> subscribers_;
    std::deque<Event> ring_;
    EventSubscriptionId next_id_ = 1;
    std::size_t published_count_ = 0;
};

}  // namespace trinity::core
