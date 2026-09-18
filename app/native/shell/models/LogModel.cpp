#include "LogModel.hpp"
#include <QMetaObject>
#include "../../core/Time.hpp"

namespace trinity::shell {

LogModel::LogModel(core::EventBus* bus, QObject* parent)
    : QAbstractListModel(parent), bus_(bus) {
    subscribe();
    refresh(500);
}

LogModel::~LogModel() { unsubscribe(); }

void LogModel::subscribe() {
    if (!bus_) return;
    auto cb = [this](const core::Event& e) {
        QString topic = QString::fromStdString(e.topic);
        QString payload = QString::fromStdString(e.payload.dump());
        QMetaObject::invokeMethod(this, [this, topic, payload]() { onBusEvent(topic, payload); },
                                  Qt::QueuedConnection);
    };
    subs_.push_back(bus_->subscribe("job.log", cb));
    subs_.push_back(bus_->subscribe("job.queued", cb));
    subs_.push_back(bus_->subscribe("job.finished", cb));
    subs_.push_back(bus_->subscribe("log.record", cb));
}
void LogModel::unsubscribe() {
    if (!bus_) return;
    for (auto id : subs_) bus_->unsubscribe(id);
    subs_.clear();
}

void LogModel::onBusEvent(const QString& topic, const QString& payload) {
    // payload is Json string - try to extract message
    LogEntry e;
    e.timestamp = QString::fromStdString(core::iso_utc_now());
    e.level = "INFO";
    e.component = topic;
    e.message = payload;
    // simple decode for job.log
    if (topic == "job.log") {
        try {
            auto j = core::Json::parse(payload.toStdString());
            if (auto* jid = j.find("job_id")) e.jobId = QString::fromStdString(jid->as_string());
            if (auto* line = j.find("line")) e.message = QString::fromStdString(line->as_string());
            else if (auto* l2 = j.find("payload")) {
                if (auto* line2 = l2->find("line")) e.message = QString::fromStdString(line2->as_string());
            }
        } catch (...) {}
    } else if (topic == "job.finished") {
        e.message = QString("job %1 finished: %2").arg(topic, payload.left(120));
    }
    beginInsertRows(QModelIndex(), 0, 0);
    allEntries_.insert(allEntries_.begin(), e);
    if (allEntries_.size() > 2048) allEntries_.pop_back();
    if (passesFilter(e)) entries_.insert(entries_.begin(), e);
    if (entries_.size() > 1000) entries_.pop_back();
    endInsertRows();
    emit countChanged();
}

int LogModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(entries_.size());
}
QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(entries_.size()))
        return {};
    const auto& e = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case TimestampRole: return e.timestamp;
        case LevelRole: return e.level;
        case ComponentRole: return e.component;
        case MessageRole: return e.message;
        case JobIdRole: return e.jobId;
        case FormattedRole: return QString("[%1] %2 %3").arg(e.timestamp, e.component, e.message);
        default: return {};
    }
}
QHash<int, QByteArray> LogModel::roleNames() const {
    return {
        {TimestampRole, "timestamp"},
        {LevelRole, "level"},
        {ComponentRole, "component"},
        {MessageRole, "message"},
        {JobIdRole, "jobId"},
        {FormattedRole, "formatted"},
    };
}

bool LogModel::passesFilter(const LogEntry& e) const {
    if (filterText_.isEmpty()) return true;
    return e.message.contains(filterText_, Qt::CaseInsensitive) ||
           e.component.contains(filterText_, Qt::CaseInsensitive) ||
           e.jobId.contains(filterText_, Qt::CaseInsensitive);
}

void LogModel::setFilterText(const QString& t) {
    if (filterText_ == t) return;
    filterText_ = t;
    emit filterTextChanged();
    beginResetModel();
    entries_.clear();
    for (auto& e : allEntries_) if (passesFilter(e)) entries_.push_back(e);
    endResetModel();
    emit countChanged();
}

void LogModel::refresh(int max) {
    // pull from logger ring + bus recent
    auto recent = core::Logger::instance().recent_records(static_cast<std::size_t>(max));
    std::vector<LogEntry> loaded;
    loaded.reserve(recent.size());
    for (auto& r : recent) {
        LogEntry e;
        e.timestamp = QString::number(r.timestamp_millis);
        e.level = QString::fromStdString(core::log_level_string(r.level));
        e.component = QString::fromStdString(r.component);
        e.message = QString::fromStdString(r.message);
        loaded.push_back(std::move(e));
    }
    if (bus_) {
        auto events = bus_->recent(static_cast<std::size_t>(max));
        for (auto& ev : events) {
            LogEntry e;
            e.timestamp = QString::fromStdString(ev.timestamp);
            e.component = QString::fromStdString(ev.topic);
            e.message = QString::fromStdString(ev.payload.dump());
            e.level = "INFO";
            loaded.push_back(std::move(e));
        }
    }
    beginResetModel();
    allEntries_ = loaded;
    entries_.clear();
    for (auto& e : allEntries_) if (passesFilter(e)) entries_.push_back(e);
    if (entries_.size() > static_cast<std::size_t>(max)) entries_.resize(static_cast<std::size_t>(max));
    endResetModel();
    emit countChanged();
}

void LogModel::clear() {
    beginResetModel();
    entries_.clear();
    allEntries_.clear();
    endResetModel();
    emit countChanged();
}

QVariantMap LogModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(entries_.size())) return out;
    const auto& e = entries_[static_cast<std::size_t>(row)];
    out["timestamp"] = e.timestamp;
    out["level"] = e.level;
    out["component"] = e.component;
    out["message"] = e.message;
    out["jobId"] = e.jobId;
    return out;
}

} // namespace trinity::shell
