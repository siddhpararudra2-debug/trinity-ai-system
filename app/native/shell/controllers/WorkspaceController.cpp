#include "WorkspaceController.hpp"
#include <QVariantMap>

namespace trinity::shell {

static struct WsInfo { const char* id; const char* title; bool available; const char* badge; const char* detail; } kWorkspaces[] = {
    {"home", "Home", true, "", ""},
    {"projects", "Projects", true, "", ""},
    {"cad", "CAD", true, "", ""},
    {"pcb", "PCB", false, "SCAFFOLD", "PCB engine scaffolded — capability_unavailable. Layout generation not implemented."},
    {"math", "Math", true, "", ""},
    {"simulation", "Simulation", false, "SCAFFOLD", "Simulation scaffolded — execution not implemented."},
    {"vision", "Vision", false, "SCAFFOLD", "Vision scaffolded — execution not implemented."},
    {"firmware", "Firmware", false, "SCAFFOLD", "Firmware scaffolded — execution not implemented."},
    {"research", "Research", false, "SCAFFOLD", "Research scaffolded — vector store not implemented."},
    {"artifacts", "Artifacts", true, "", ""},
    {"jobs", "Jobs", true, "", ""},
    {"settings", "Settings", true, "", ""},
};

WorkspaceController::WorkspaceController(QObject* parent) : QObject(parent) {}

QString WorkspaceController::currentWorkspaceId() const {
    if (current_>=0 && current_ < 12) return kWorkspaces[current_].id;
    return "home";
}
QString WorkspaceController::currentWorkspaceTitle() const {
    if (current_>=0 && current_ < 12) return kWorkspaces[current_].title;
    return "Home";
}

void WorkspaceController::setCurrentWorkspace(int ws) {
    if (ws<0 || ws>=12) return;
    if (current_!=ws) {
        current_=ws;
        emit currentWorkspaceChanged();
        emit workspaceRequested(currentWorkspaceId());
    }
}

void WorkspaceController::switchTo(const QString& id) {
    for (int i=0;i<12;++i) if (id==kWorkspaces[i].id) { setCurrentWorkspace(i); return; }
}

QStringList WorkspaceController::workspaceIds() const {
    QStringList out;
    for (auto& w: kWorkspaces) out<<w.id;
    return out;
}
QStringList WorkspaceController::workspaceTitles() const {
    QStringList out;
    for (auto& w: kWorkspaces) out<<w.title;
    return out;
}

QVariantList WorkspaceController::workspaceList() const {
    QVariantList out;
    for (auto& w: kWorkspaces) {
        QVariantMap m;
        m["id"]=w.id; m["title"]=w.title; m["available"]=w.available; m["badge"]=w.badge; m["detail"]=w.detail;
        out.append(m);
    }
    return out;
}
bool WorkspaceController::isAvailable(const QString& id) const {
    for (auto& w: kWorkspaces) if (id==w.id) return w.available;
    return false;
}
QString WorkspaceController::availabilityDetail(const QString& id) const {
    for (auto& w: kWorkspaces) if (id==w.id) return w.detail;
    return {};
}

} // namespace trinity::shell
