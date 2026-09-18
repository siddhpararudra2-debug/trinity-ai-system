#include "LayoutController.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>

namespace trinity::shell {

LayoutController::LayoutController(const QString& layoutFile, QObject* parent)
    : QObject(parent), layoutFile_(layoutFile) {
    load();
}

void LayoutController::setLeftWidth(int v) { if (leftWidth_!=v) { leftWidth_=qBound(180, v, 480); emit layoutChanged(); } }
void LayoutController::setLeftCollapsed(bool v) { if (leftCollapsed_!=v) { leftCollapsed_=v; emit layoutChanged(); } }
void LayoutController::setLeftUndocked(bool v) { if (leftUndocked_!=v) { leftUndocked_=v; emit layoutChanged(); } }
void LayoutController::setRightWidth(int v) { if (rightWidth_!=v) { rightWidth_=qBound(220, v, 520); emit layoutChanged(); } }
void LayoutController::setRightCollapsed(bool v) { if (rightCollapsed_!=v) { rightCollapsed_=v; emit layoutChanged(); } }
void LayoutController::setRightUndocked(bool v) { if (rightUndocked_!=v) { rightUndocked_=v; emit layoutChanged(); } }
void LayoutController::setBottomHeight(int v) { if (bottomHeight_!=v) { bottomHeight_=qBound(120, v, 600); emit layoutChanged(); } }
void LayoutController::setBottomCollapsed(bool v) { if (bottomCollapsed_!=v) { bottomCollapsed_=v; emit layoutChanged(); } }
void LayoutController::setBottomUndocked(bool v) { if (bottomUndocked_!=v) { bottomUndocked_=v; emit layoutChanged(); } }
void LayoutController::setBottomTab(int v) { if (bottomTab_!=v) { bottomTab_=v; emit layoutChanged(); } }
void LayoutController::setWindowWidth(int v) { if (windowWidth_!=v) { windowWidth_=qBound(900, v, 3840); emit layoutChanged(); } }
void LayoutController::setWindowHeight(int v) { if (windowHeight_!=v) { windowHeight_=qBound(600, v, 2160); emit layoutChanged(); } }
void LayoutController::setWindowX(int v) { if (windowX_!=v) { windowX_=v; emit layoutChanged(); } }
void LayoutController::setWindowY(int v) { if (windowY_!=v) { windowY_=v; emit layoutChanged(); } }

void LayoutController::load() {
    QFile f(layoutFile_);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    auto o = doc.object();
    leftWidth_ = o.value("leftWidth").toInt(leftWidth_);
    leftCollapsed_ = o.value("leftCollapsed").toBool(leftCollapsed_);
    leftUndocked_ = o.value("leftUndocked").toBool(leftUndocked_);
    rightWidth_ = o.value("rightWidth").toInt(rightWidth_);
    rightCollapsed_ = o.value("rightCollapsed").toBool(rightCollapsed_);
    rightUndocked_ = o.value("rightUndocked").toBool(rightUndocked_);
    bottomHeight_ = o.value("bottomHeight").toInt(bottomHeight_);
    bottomCollapsed_ = o.value("bottomCollapsed").toBool(bottomCollapsed_);
    bottomUndocked_ = o.value("bottomUndocked").toBool(bottomUndocked_);
    bottomTab_ = o.value("bottomTab").toInt(bottomTab_);
    windowWidth_ = o.value("windowWidth").toInt(windowWidth_);
    windowHeight_ = o.value("windowHeight").toInt(windowHeight_);
    windowX_ = o.value("windowX").toInt(windowX_);
    windowY_ = o.value("windowY").toInt(windowY_);
    emit layoutChanged();
}

void LayoutController::save() {
    QDir().mkpath(QFileInfo(layoutFile_).absolutePath());
    QJsonObject o;
    o["version"]=1;
    o["leftWidth"]=leftWidth_;
    o["leftCollapsed"]=leftCollapsed_;
    o["leftUndocked"]=leftUndocked_;
    o["rightWidth"]=rightWidth_;
    o["rightCollapsed"]=rightCollapsed_;
    o["rightUndocked"]=rightUndocked_;
    o["bottomHeight"]=bottomHeight_;
    o["bottomCollapsed"]=bottomCollapsed_;
    o["bottomUndocked"]=bottomUndocked_;
    o["bottomTab"]=bottomTab_;
    o["windowWidth"]=windowWidth_;
    o["windowHeight"]=windowHeight_;
    o["windowX"]=windowX_;
    o["windowY"]=windowY_;
    QFile f(layoutFile_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void LayoutController::restoreDefaults() {
    leftWidth_=260; leftCollapsed_=false; leftUndocked_=false;
    rightWidth_=320; rightCollapsed_=false; rightUndocked_=false;
    bottomHeight_=240; bottomCollapsed_=false; bottomUndocked_=false;
    bottomTab_=0;
    emit layoutChanged();
    emit layoutRestored();
    save();
}

void LayoutController::toggleLeft() { setLeftCollapsed(!leftCollapsed_); save(); }
void LayoutController::toggleRight() { setRightCollapsed(!rightCollapsed_); save(); }
void LayoutController::toggleBottom() { setBottomCollapsed(!bottomCollapsed_); save(); }

} // namespace trinity::shell
