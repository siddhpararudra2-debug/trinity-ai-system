#include "JobModel.hpp"

#include <QMetaObject>

namespace trinity::shell {

JobModel::JobModel(jobs::JobSystem* jobs, core::EventBus* bus, QObject* parent)
    : QAbstractListModel(parent), jobs_(jobs), bus_(bus) {
    coalesceTimer_ = new QTimer(this);
    coalesceTimer_->setSingleShot(true);
    coalesceTimer_->setInterval(80);
    connect(coalesceTimer_, &QTimer::timeout, this, [this]() { refresh(100); });
    subscribe();
    refresh(100);
}

JobModel::~JobModel() { unsubscribe(); }

void JobModel::subscribe() {
    if (!bus_) return;
    auto wrap = [this](const core::Event& e) {
        const QString topic = QString::fromStdString(e.topic);
        const QString payload = QString::fromStdString(e.payload.dump());
        QMetaObject::invokeMethod(this, [this, topic, payload]() { onJobEvent(topic, payload); },
                                  Qt::QueuedConnection);
    };
    subs_.push_back(bus_->subscribe("job.queued", wrap));
    subs_.push_back(bus_->subscribe("job.progress", wrap));
    subs_.push_back(bus_->subscribe("job.finished", wrap));
    subs_.push_back(bus_->subscribe("job.log", wrap));
}

void JobModel::unsubscribe() {
    if (!bus_) return;
    for (auto id : subs_) bus_->unsubscribe(id);
    subs_.clear();
}

void JobModel::onJobEvent(const QString& /*topic*/, const QString& /*payloadJson*/) {
    if (!coalesceTimer_->isActive()) coalesceTimer_->start();
}

int JobModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(records_.size());
}

QVariant JobModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(records_.size()))
        return {};
    const auto& r = records_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case JobIdRole: return QString::fromStdString(r.job_id);
        case ProjectIdRole: return QString::fromStdString(r.project_id);
        case EngineRole: return QString::fromStdString(r.engine);
        case OperationRole: return QString::fromStdString(r.operation);
        case StatusRole: return QString::fromStdString(jobs::job_state_string(r.status));
        case ProgressRole: return r.progress;
        case CreatedAtRole: return QString::fromStdString(r.created_at);
        case UpdatedAtRole: return QString::fromStdString(r.updated_at);
        case DurationRole: return static_cast<qlonglong>(r.duration_ms);
        case HasErrorRole: return r.error.has_value();
        case ErrorMessageRole: return r.error ? QString::fromStdString(r.error->message()) : QString();
        default: return {};
    }
}

QHash<int, QByteArray> JobModel::roleNames() const {
    return {
        {JobIdRole, "jobId"},
        {ProjectIdRole, "projectId"},
        {EngineRole, "engine"},
        {OperationRole, "operation"},
        {StatusRole, "status"},
        {ProgressRole, "progress"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
        {DurationRole, "durationMs"},
        {HasErrorRole, "hasError"},
        {ErrorMessageRole, "errorMessage"},
    };
}

int JobModel::queuedCount() const { return jobs_ ? static_cast<int>(jobs_->queued_count()) : 0; }
int JobModel::activeCount() const { return jobs_ ? static_cast<int>(jobs_->active_count()) : 0; }

void JobModel::refresh(int limit) {
    if (!jobs_) return;
    auto result = jobs_->list_recent(static_cast<std::size_t>(limit > 0 ? limit : 100));
    if (result.is_error()) {
        emit errorRaised(QString::fromStdString(result.error().code_string()),
                         QString::fromStdString(result.error().message()));
        return;
    }
    beginResetModel();
    records_ = result.take_value();
    endResetModel();
    emit countChanged();
    emit countsChanged();
}

QVariantMap JobModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(records_.size())) return out;
    const auto& r = records_[static_cast<std::size_t>(row)];
    out["jobId"] = QString::fromStdString(r.job_id);
    out["projectId"] = QString::fromStdString(r.project_id);
    out["engine"] = QString::fromStdString(r.engine);
    out["operation"] = QString::fromStdString(r.operation);
    out["status"] = QString::fromStdString(jobs::job_state_string(r.status));
    out["progress"] = r.progress;
    out["createdAt"] = QString::fromStdString(r.created_at);
    out["updatedAt"] = QString::fromStdString(r.updated_at);
    out["durationMs"] = static_cast<qlonglong>(r.duration_ms);
    out["hasError"] = r.error.has_value();
    out["errorMessage"] = r.error ? QString::fromStdString(r.error->message()) : QString();
    return out;
}

QVariantMap JobModel::getJob(const QString& jobId) const {
    if (!jobs_) return {};
    auto res = jobs_->get(jobId.toStdString());
    if (res.is_error()) return {};
    const auto& r = res.value();
    QVariantMap out;
    out["jobId"] = QString::fromStdString(r.job_id);
    out["projectId"] = QString::fromStdString(r.project_id);
    out["engine"] = QString::fromStdString(r.engine);
    out["operation"] = QString::fromStdString(r.operation);
    out["status"] = QString::fromStdString(jobs::job_state_string(r.status));
    out["progress"] = r.progress;
    out["durationMs"] = static_cast<qlonglong>(r.duration_ms);
    return out;
}

bool JobModel::pauseJob(const QString& jobId) {
    if (!jobs_) return false;
    auto s = jobs_->pause(jobId.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()),
                         QString::fromStdString(s.error().message()));
        return false;
    }
    refresh(100);
    return true;
}
bool JobModel::resumeJob(const QString& jobId) {
    if (!jobs_) return false;
    auto s = jobs_->resume(jobId.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()),
                         QString::fromStdString(s.error().message()));
        return false;
    }
    refresh(100);
    return true;
}
bool JobModel::cancelJob(const QString& jobId) {
    if (!jobs_) return false;
    auto s = jobs_->cancel(jobId.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()),
                         QString::fromStdString(s.error().message()));
        return false;
    }
    refresh(100);
    return true;
}

} // namespace trinity::shell
