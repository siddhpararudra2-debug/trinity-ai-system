// Trinity — WorkspaceController: 12 workspaces, keyboard routing, availability.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace trinity::shell {

class WorkspaceController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int currentWorkspace READ currentWorkspace WRITE setCurrentWorkspace NOTIFY currentWorkspaceChanged)
    Q_PROPERTY(QString currentWorkspaceId READ currentWorkspaceId NOTIFY currentWorkspaceChanged)
    Q_PROPERTY(QString currentWorkspaceTitle READ currentWorkspaceTitle NOTIFY currentWorkspaceChanged)

public:
    enum Workspace {
        Home = 0,
        Projects = 1,
        Cad = 2,
        Pcb = 3,
        Math = 4,
        Simulation = 5,
        Vision = 6,
        Firmware = 7,
        Research = 8,
        Artifacts = 9,
        Jobs = 10,
        Settings = 11
    };
    Q_ENUM(Workspace)

    explicit WorkspaceController(QObject* parent = nullptr);

    int currentWorkspace() const { return current_; }
    QString currentWorkspaceId() const;
    QString currentWorkspaceTitle() const;

    Q_INVOKABLE void setCurrentWorkspace(int ws);
    Q_INVOKABLE void switchTo(const QString& id);
    Q_INVOKABLE QStringList workspaceIds() const;
    Q_INVOKABLE QStringList workspaceTitles() const;
    Q_INVOKABLE QVariantList workspaceList() const; // {id,title,available,badge}
    Q_INVOKABLE bool isAvailable(const QString& id) const;
    Q_INVOKABLE QString availabilityDetail(const QString& id) const;

signals:
    void currentWorkspaceChanged();
    void workspaceRequested(const QString& id);

private:
    int current_ = Home;
};

} // namespace trinity::shell
