// Trinity — EngineModel: read-only registry view.
#pragma once

#include <QAbstractListModel>

#include "../../engines/Engine.hpp"

namespace trinity::shell {

class EngineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        VersionRole,
        CapabilitiesRole,
        HealthRole,
        HealthDetailRole,
        InputSchemaRole,
        OutputSchemaRole
    };
    Q_ENUM(Roles)

    explicit EngineModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QString healthString(int row) const;

signals:
    void countChanged();

private:
    std::vector<engines::EngineDescriptor> engines_;
};

} // namespace trinity::shell
