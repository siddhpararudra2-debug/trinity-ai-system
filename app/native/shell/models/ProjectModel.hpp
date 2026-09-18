// Trinity — ProjectModel: QAbstractListModel over ProjectStore.
// Every row is a live project record; the model refreshes from SQLite and notifies QML.
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include "../../projects/Project.hpp"

namespace trinity::shell {

class ProjectModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString activeProjectId READ activeProjectId WRITE setActiveProjectId NOTIFY activeProjectIdChanged)

public:
    enum Roles {
        ProjectIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        WorkspaceRole,
        ArchivedRole,
        CreatedAtRole,
        UpdatedAtRole
    };
    Q_ENUM(Roles)

    explicit ProjectModel(projects::ProjectStore* store, QObject* parent = nullptr);

    // QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeProjectId() const { return activeProjectId_; }
    void setActiveProjectId(const QString& id);

    Q_INVOKABLE void refresh(bool includeArchived = true);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap createProject(const QString& name, const QString& description);
    Q_INVOKABLE bool openProject(const QString& projectId);
    Q_INVOKABLE bool archiveProject(const QString& projectId, bool archived);
    Q_INVOKABLE bool renameProject(const QString& projectId, const QString& newName);

signals:
    void countChanged();
    void activeProjectIdChanged();
    void errorRaised(const QString& code, const QString& message);
    void projectCreated(const QString& projectId);

private:
    projects::ProjectStore* store_;
    std::vector<projects::Project> projects_;
    QString activeProjectId_;
};

} // namespace trinity::shell
