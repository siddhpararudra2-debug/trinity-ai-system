// Trinity — ArtifactModel: artifact list filtered by active project.
#pragma once

#include <QAbstractListModel>

#include "../../artifacts/Artifact.hpp"
#include "../../core/EventBus.hpp"

namespace trinity::shell {

class ArtifactModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString activeProjectId READ activeProjectId WRITE setActiveProjectId NOTIFY activeProjectIdChanged)

public:
    enum Roles {
        ArtifactIdRole = Qt::UserRole + 1,
        ProjectIdRole,
        JobIdRole,
        TypeRole,
        FilenameRole,
        PathRole,
        SizeBytesRole,
        HashRole,
        EngineRole,
        EngineVersionRole,
        ValidationStateRole,
        CreatedAtRole
    };
    Q_ENUM(Roles)

    explicit ArtifactModel(artifacts::ArtifactStore* store, core::EventBus* bus, QObject* parent = nullptr);
    ~ArtifactModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString activeProjectId() const { return activeProjectId_; }
    void setActiveProjectId(const QString& id);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool verifyIntegrity(const QString& artifactId);
    Q_INVOKABLE bool removeArtifact(const QString& artifactId);

signals:
    void countChanged();
    void activeProjectIdChanged();
    void errorRaised(const QString& code, const QString& message);

private:
    void subscribe();
    void unsubscribe();

    artifacts::ArtifactStore* store_;
    core::EventBus* bus_;
    std::vector<artifacts::Artifact> artifacts_;
    QString activeProjectId_;
    std::vector<core::EventSubscriptionId> subs_;
};

} // namespace trinity::shell
