// Trinity — CommandModel: palette entries from CommandRegistry.
#pragma once

#include <QAbstractListModel>
#include <QSortFilterProxyModel>

#include "../../commands/CommandRegistry.hpp"

namespace trinity::shell {

class CommandModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        CommandIdRole = Qt::UserRole + 1,
        TitleRole,
        CategoryRole,
        KeywordsRole,
        SearchTextRole
    };
    Q_ENUM(Roles)

    explicit CommandModel(commands::CommandRegistry* registry, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool execute(const QString& commandId, const QVariantMap& args = {});

signals:
    void countChanged();
    void errorRaised(const QString& code, const QString& message);
    void commandExecuted(const QString& commandId, const QString& resultJson);

private:
    struct Entry {
        QString commandId;
        QString title;
        QString category;
        QStringList keywords;
        QString searchText;
    };
    commands::CommandRegistry* registry_;
    std::vector<Entry> entries_;
};

// Proxy that filters on title/category/keywords
class CommandFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)

public:
    explicit CommandFilterModel(QObject* parent = nullptr);

    QString filterText() const { return filterText_; }
    void setFilterText(const QString& t);

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool executeFiltered(int row, const QVariantMap& args = {});

signals:
    void filterTextChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QString filterText_;
};

} // namespace trinity::shell
