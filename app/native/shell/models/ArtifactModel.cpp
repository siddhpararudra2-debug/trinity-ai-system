#include "ArtifactModel.hpp"
#include <QMetaObject>

namespace trinity::shell {

ArtifactModel::ArtifactModel(artifacts::ArtifactStore* store, core::EventBus* bus, QObject* parent)
    : QAbstractListModel(parent), store_(store), bus_(bus) {
    subscribe();
}

ArtifactModel::~ArtifactModel() { unsubscribe(); }

void ArtifactModel::subscribe() {
    if (!bus_) return;
    auto cb = [this](const core::Event& e) {
        // any artifact or job finish should trigger refresh
        (void)e;
        QMetaObject::invokeMethod(this, [this]() { refresh(); }, Qt::QueuedConnection);
    };
    subs_.push_back(bus_->subscribe("artifact.stored", cb));
    subs_.push_back(bus_->subscribe("validation.recorded", cb));
    subs_.push_back(bus_->subscribe("job.finished", cb));
}
void ArtifactModel::unsubscribe() {
    if (!bus_) return;
    for (auto id : subs_) bus_->unsubscribe(id);
    subs_.clear();
}

int ArtifactModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(artifacts_.size());
}

QVariant ArtifactModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(artifacts_.size()))
        return {};
    const auto& a = artifacts_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case ArtifactIdRole: return QString::fromStdString(a.artifact_id);
        case ProjectIdRole: return QString::fromStdString(a.project_id);
        case JobIdRole: return QString::fromStdString(a.job_id);
        case TypeRole: return QString::fromStdString(a.type);
        case FilenameRole: return QString::fromStdString(a.filename);
        case PathRole: return QString::fromStdString(a.path);
        case SizeBytesRole: return static_cast<qlonglong>(a.size_bytes);
        case HashRole: return QString::fromStdString(a.hash_sha256);
        case EngineRole: return QString::fromStdString(a.engine);
        case EngineVersionRole: return QString::fromStdString(a.engine_version);
        case ValidationStateRole: return QString::fromStdString(artifacts::validation_state_string(a.validation_state));
        case CreatedAtRole: return QString::fromStdString(a.created_at);
        default: return {};
    }
}

QHash<int, QByteArray> ArtifactModel::roleNames() const {
    return {
        {ArtifactIdRole, "artifactId"},
        {ProjectIdRole, "projectId"},
        {JobIdRole, "jobId"},
        {TypeRole, "type"},
        {FilenameRole, "filename"},
        {PathRole, "path"},
        {SizeBytesRole, "sizeBytes"},
        {HashRole, "hash"},
        {EngineRole, "engine"},
        {EngineVersionRole, "engineVersion"},
        {ValidationStateRole, "validationState"},
        {CreatedAtRole, "createdAt"},
    };
}

void ArtifactModel::setActiveProjectId(const QString& id) {
    if (activeProjectId_ != id) {
        activeProjectId_ = id;
        emit activeProjectIdChanged();
        refresh();
    }
}

void ArtifactModel::refresh() {
    if (!store_) return;
    if (activeProjectId_.isEmpty()) {
        beginResetModel();
        artifacts_.clear();
        endResetModel();
        emit countChanged();
        return;
    }
    auto res = store_->list_for_project(activeProjectId_.toStdString());
    if (res.is_error()) {
        emit errorRaised(QString::fromStdString(res.error().code_string()),
                         QString::fromStdString(res.error().message()));
        return;
    }
    beginResetModel();
    artifacts_ = res.take_value();
    endResetModel();
    emit countChanged();
}

QVariantMap ArtifactModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(artifacts_.size())) return out;
    const auto& a = artifacts_[static_cast<std::size_t>(row)];
    out["artifactId"] = QString::fromStdString(a.artifact_id);
    out["projectId"] = QString::fromStdString(a.project_id);
    out["jobId"] = QString::fromStdString(a.job_id);
    out["type"] = QString::fromStdString(a.type);
    out["filename"] = QString::fromStdString(a.filename);
    out["path"] = QString::fromStdString(a.path);
    out["sizeBytes"] = static_cast<qlonglong>(a.size_bytes);
    out["hash"] = QString::fromStdString(a.hash_sha256);
    out["engine"] = QString::fromStdString(a.engine);
    out["validationState"] = QString::fromStdString(artifacts::validation_state_string(a.validation_state));
    out["createdAt"] = QString::fromStdString(a.created_at);
    return out;
}

bool ArtifactModel::verifyIntegrity(const QString& artifactId) {
    if (!store_) return false;
    auto res = store_->verify_integrity(artifactId.toStdString());
    if (res.is_error()) {
        emit errorRaised(QString::fromStdString(res.error().code_string()),
                         QString::fromStdString(res.error().message()));
        return false;
    }
    return res.value();
}

bool ArtifactModel::removeArtifact(const QString& artifactId) {
    if (!store_) return false;
    auto s = store_->remove(artifactId.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()),
                         QString::fromStdString(s.error().message()));
        return false;
    }
    refresh();
    return true;
}

} // namespace trinity::shell
