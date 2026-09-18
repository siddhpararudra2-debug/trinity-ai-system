#include "EngineModel.hpp"

namespace trinity::shell {

EngineModel::EngineModel(QObject* parent) : QAbstractListModel(parent) { refresh(); }

int EngineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(engines_.size());
}

QVariant EngineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(engines_.size()))
        return {};
    const auto& e = engines_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case IdRole: return QString::fromStdString(e.id);
        case NameRole: return QString::fromStdString(e.name);
        case VersionRole: return QString::fromStdString(e.version);
        case CapabilitiesRole: {
            QStringList caps;
            for (auto& c : e.capabilities) caps << QString::fromStdString(c);
            return caps;
        }
        case HealthRole: return QString::fromStdString(engines::engine_health_string(e.health));
        case HealthDetailRole: return QString::fromStdString(e.health_detail);
        case InputSchemaRole: return QString::fromStdString(e.input_schema.to_json().dump());
        case OutputSchemaRole: return QString::fromStdString(e.output_schema.to_json().dump());
        default: return {};
    }
}

QHash<int, QByteArray> EngineModel::roleNames() const {
    return {
        {IdRole, "engineId"},
        {NameRole, "name"},
        {VersionRole, "version"},
        {CapabilitiesRole, "capabilities"},
        {HealthRole, "health"},
        {HealthDetailRole, "healthDetail"},
        {InputSchemaRole, "inputSchema"},
        {OutputSchemaRole, "outputSchema"},
    };
}

void EngineModel::refresh() {
    beginResetModel();
    engines_ = engines::EngineRegistry::instance().list();
    endResetModel();
    emit countChanged();
}

QVariantMap EngineModel::get(int row) const {
    QVariantMap out;
    if (row < 0 || row >= static_cast<int>(engines_.size())) return out;
    const auto& e = engines_[static_cast<std::size_t>(row)];
    out["engineId"] = QString::fromStdString(e.id);
    out["name"] = QString::fromStdString(e.name);
    out["version"] = QString::fromStdString(e.version);
    QStringList caps;
    for (auto& c : e.capabilities) caps << QString::fromStdString(c);
    out["capabilities"] = caps;
    out["health"] = QString::fromStdString(engines::engine_health_string(e.health));
    out["healthDetail"] = QString::fromStdString(e.health_detail);
    return out;
}

QString EngineModel::healthString(int row) const {
    if (row < 0 || row >= static_cast<int>(engines_.size())) return {};
    return QString::fromStdString(engines::engine_health_string(engines_[static_cast<std::size_t>(row)].health));
}

} // namespace trinity::shell
