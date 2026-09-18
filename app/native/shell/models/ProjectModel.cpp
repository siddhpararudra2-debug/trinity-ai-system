#include "ProjectModel.hpp"

#include <QMetaObject>

namespace trinity::shell {

ProjectModel::ProjectModel(projects::ProjectStore* store, QObject* parent)
    : QAbstractListModel(parent), store_(store) {}

int ProjectModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(projects_.size());
}

QVariant ProjectModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(projects_.size()))
        return {};
    const auto& p = projects_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case ProjectIdRole: return QString::fromStdString(p.project_id);
        case NameRole: return QString::fromStdString(p.name);
        case DescriptionRole: return QString::fromStdString(p.description);
        case WorkspaceRole: return QString::fromStdString(p.workspace);
        case ArchivedRole: return p.archived;
        case CreatedAtRole: return QString::fromStdString(p.created_at);
        case UpdatedAtRole: return QString::fromStdString(p.updated_at);
        default: return {};
    }
}

QHash<int, QByteArray> ProjectModel::roleNames() const {
    return {
        {ProjectIdRole, "projectId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {WorkspaceRole, "workspace"},
        {ArchivedRole, "archived"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
    };
}

void ProjectModel::setActiveProjectId(const QString& id) {
    if (activeProjectId_ != id) {
        activeProjectId_ = id;
        emit activeProjectIdChanged();
    }
}

void ProjectModel::refresh(bool includeArchived) {
    if (!store_) return;
    auto result = store_->list(includeArchived);
    if (result.is_error()) {
        emit errorRaised(QString::fromStdString(result.error().code_string()),
                         QString::fromStdString(result.error().message()));
        return;
    }
    beginResetModel();
    projects_ = result.take_value();
    endResetModel();
    emit countChanged();
}

QVariantMap ProjectModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(projects_.size())) return out;
    const auto& p = projects_[static_cast<std::size_t>(row)];
    out["projectId"] = QString::fromStdString(p.project_id);
    out["name"] = QString::fromStdString(p.name);
    out["description"] = QString::fromStdString(p.description);
    out["workspace"] = QString::fromStdString(p.workspace);
    out["archived"] = p.archived;
    out["createdAt"] = QString::fromStdString(p.created_at);
    out["updatedAt"] = QString::fromStdString(p.updated_at);
    return out;
}

QVariantMap ProjectModel::createProject(const QString& name, const QString& description) {
    QVariantMap out;
    if (!store_) return out;
    auto result = store_->create(name.toStdString(), description.toStdString());
    if (result.is_error()) {
        emit errorRaised(QString::fromStdString(result.error().code_string()),
                         QString::fromStdString(result.error().message()));
        return out;
    }
    const auto& p = result.value();
    out["projectId"] = QString::fromStdString(p.project_id);
    out["name"] = QString::fromStdString(p.name);
    out["workspace"] = QString::fromStdString(p.workspace);
    setActiveProjectId(QString::fromStdString(p.project_id));
    refresh(true);
    emit projectCreated(QString::fromStdString(p.project_id));
    return out;
}

bool ProjectModel::openProject(const QString& projectId) {
    if (!store_) return false;
    auto fetched = store_->get(projectId.toStdString());
    if (fetched.is_error()) {
        emit errorRaised(QString::fromStdString(fetched.error().code_string()),
                         QString::fromStdString(fetched.error().message()));
        return false;
    }
    setActiveProjectId(projectId);
    return true;
}

bool ProjectModel::archiveProject(const QString& projectId, bool archived) {
    if (!store_) return false;
    auto res = store_->archive(projectId.toStdString(), archived);
    if (res.is_error()) {
        emit errorRaised(QString::fromStdString(res.error().code_string()),
                         QString::fromStdString(res.error().message()));
        return false;
    }
    refresh(true);
    return true;
}

bool ProjectModel::renameProject(const QString& projectId, const QString& newName) {
    if (!store_) return false;
    auto res = store_->rename(projectId.toStdString(), newName.toStdString());
    if (res.is_error()) {
        emit errorRaised(QString::fromStdString(res.error().code_string()),
                         QString::fromStdString(res.error().message()));
        return false;
    }
    refresh(true);
    return true;
}

} // namespace trinity::shell
