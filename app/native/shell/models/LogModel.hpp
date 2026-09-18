// Trinity — LogModel: tail of logger + EventBus job logs.
#pragma once

#include <QAbstractListModel>
#include <QTimer>

#include "../../core/Logging.hpp"
#include "../../core/EventBus.hpp"

namespace trinity::shell {

struct LogEntry {
    QString timestamp;
    QString level;
    QString component;
    QString message;
    QString jobId;
};

class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,
        LevelRole,
        ComponentRole,
        MessageRole,
        JobIdRole,
        FormattedRole
    };
    Q_ENUM(Roles)

    explicit LogModel(core::EventBus* bus, QObject* parent = nullptr);
    ~LogModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filterText() const { return filterText_; }
    void setFilterText(const QString& t);

    Q_INVOKABLE void refresh(int max = 500);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap get(int row) const;

signals:
    void countChanged();
    void filterTextChanged();

private slots:
    void onBusEvent(const QString& topic, const QString& payload);

private:
    void subscribe();
    void unsubscribe();
    bool passesFilter(const LogEntry& e) const;

    core::EventBus* bus_;
    std::vector<LogEntry> entries_;
    std::vector<LogEntry> allEntries_;
    QString filterText_;
    std::vector<core::EventSubscriptionId> subs_;
};

} // namespace trinity::shell
