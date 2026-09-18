// Trinity — JobModel: live job list bound to JobSystem + EventBus.
#pragma once

#include <QAbstractListModel>
#include <QTimer>

#include "../../jobs/JobSystem.hpp"
#include "../../core/EventBus.hpp"

namespace trinity::shell {

class JobModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int queuedCount READ queuedCount NOTIFY countsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countsChanged)

public:
    enum Roles {
        JobIdRole = Qt::UserRole + 1,
        ProjectIdRole,
        EngineRole,
        OperationRole,
        StatusRole,
        ProgressRole,
        CreatedAtRole,
        UpdatedAtRole,
        DurationRole,
        HasErrorRole,
        ErrorMessageRole
    };
    Q_ENUM(Roles)

    explicit JobModel(jobs::JobSystem* jobs, core::EventBus* bus, QObject* parent = nullptr);
    ~JobModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int queuedCount() const;
    int activeCount() const;

    Q_INVOKABLE void refresh(int limit = 100);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool pauseJob(const QString& jobId);
    Q_INVOKABLE bool resumeJob(const QString& jobId);
    Q_INVOKABLE bool cancelJob(const QString& jobId);
    Q_INVOKABLE QVariantMap getJob(const QString& jobId) const;

signals:
    void countChanged();
    void countsChanged();
    void jobUpdated(const QString& jobId);
    void errorRaised(const QString& code, const QString& message);

private slots:
    void onJobEvent(const QString& topic, const QString& payloadJson);

private:
    void subscribe();
    void unsubscribe();

    jobs::JobSystem* jobs_;
    core::EventBus* bus_;
    std::vector<jobs::JobRecord> records_;
    std::vector<core::EventSubscriptionId> subs_;
    QTimer* coalesceTimer_ = nullptr;
};

} // namespace trinity::shell
