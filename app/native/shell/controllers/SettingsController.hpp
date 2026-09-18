// Trinity — SettingsController: typed QML facade over SettingsStore.
#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include "../../settings/Settings.hpp"

namespace trinity::shell {

class SettingsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList categories READ categories CONSTANT)

public:
    explicit SettingsController(settings::SettingsStore* store, QObject* parent = nullptr);

    QStringList categories() const;

    Q_INVOKABLE QVariantList categoryEntries(const QString& category) const;
    Q_INVOKABLE QVariantMap allSettings() const;
    Q_INVOKABLE QString get(const QString& key) const;
    Q_INVOKABLE bool set(const QString& key, const QString& value);
    Q_INVOKABLE void resetCategory(const QString& category);
    Q_INVOKABLE QString workspaceRoot() const;
    Q_INVOKABLE bool setWorkspaceRoot(const QString& path);

signals:
    void settingChanged(const QString& key, const QString& value);
    void errorRaised(const QString& code, const QString& message);

private:
    settings::SettingsStore* store_;
};

} // namespace trinity::shell
