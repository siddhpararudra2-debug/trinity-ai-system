#include "SettingsController.hpp"

namespace trinity::shell {

SettingsController::SettingsController(settings::SettingsStore* store, QObject* parent)
    : QObject(parent), store_(store) {}

QStringList SettingsController::categories() const {
    return {"General","Appearance","Workspace","Engines","CAD","Rendering","Performance","Python","Plugins","Model Provider","Logging","Updates"};
}

QVariantList SettingsController::categoryEntries(const QString& category) const {
    QVariantList out;
    if (!store_) return out;
    auto j = store_->to_json();
    auto it = j.as_object().find(category.toStdString());
    if (it==j.as_object().end()) return out;
    for (auto& entry : it->second.as_array()) {
        QVariantMap m;
        m["key"] = QString::fromStdString(entry.find("key")->as_string());
        m["label"] = QString::fromStdString(entry.find("label")->as_string());
        m["value"] = QString::fromStdString(entry.find("value")->as_string());
        m["defaultValue"] = QString::fromStdString(entry.find("default")->as_string());
        out.append(m);
    }
    return out;
}

QVariantMap SettingsController::allSettings() const {
    QVariantMap out;
    if (!store_) return out;
    auto j = store_->to_json();
    for (auto& [cat, entries] : j.as_object()) {
        QVariantList list;
        for (auto& entry : entries.as_array()) {
            QVariantMap m;
            m["key"] = QString::fromStdString(entry.find("key")->as_string());
            m["label"] = QString::fromStdString(entry.find("label")->as_string());
            m["value"] = QString::fromStdString(entry.find("value")->as_string());
            m["defaultValue"] = QString::fromStdString(entry.find("default")->as_string());
            list.append(m);
        }
        out[QString::fromStdString(cat)] = list;
    }
    return out;
}

QString SettingsController::get(const QString& key) const {
    if (!store_) return {};
    auto r = store_->get(key.toStdString());
    if (r.is_error()) return {};
    return QString::fromStdString(r.value());
}

bool SettingsController::set(const QString& key, const QString& value) {
    if (!store_) return false;
    auto s = store_->set(key.toStdString(), value.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()), QString::fromStdString(s.error().message()));
        return false;
    }
    emit settingChanged(key, value);
    return true;
}

void SettingsController::resetCategory(const QString& category) {
    auto entries = categoryEntries(category);
    for (auto& v : entries) {
        auto m = v.toMap();
        set(m["key"].toString(), m["defaultValue"].toString());
    }
}

QString SettingsController::workspaceRoot() const {
    if (!store_) return {};
    auto r = store_->workspace_root();
    return r.is_ok() ? QString::fromStdString(r.value()) : QString();
}
bool SettingsController::setWorkspaceRoot(const QString& path) {
    if (!store_) return false;
    auto s = store_->set_workspace_root(path.toStdString());
    if (s.is_error()) {
        emit errorRaised(QString::fromStdString(s.error().code_string()), QString::fromStdString(s.error().message()));
        return false;
    }
    emit settingChanged("workspace.root", path);
    return true;
}

} // namespace trinity::shell
