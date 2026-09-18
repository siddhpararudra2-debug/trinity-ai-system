// Trinity — LayoutController: dock state persistence + collapse/dock/undock.
#pragma once

#include <QObject>
#include <QRect>
#include <QString>

namespace trinity::shell {

class LayoutController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int leftWidth READ leftWidth WRITE setLeftWidth NOTIFY layoutChanged)
    Q_PROPERTY(bool leftCollapsed READ leftCollapsed WRITE setLeftCollapsed NOTIFY layoutChanged)
    Q_PROPERTY(bool leftUndocked READ leftUndocked WRITE setLeftUndocked NOTIFY layoutChanged)
    Q_PROPERTY(int rightWidth READ rightWidth WRITE setRightWidth NOTIFY layoutChanged)
    Q_PROPERTY(bool rightCollapsed READ rightCollapsed WRITE setRightCollapsed NOTIFY layoutChanged)
    Q_PROPERTY(bool rightUndocked READ rightUndocked WRITE setRightUndocked NOTIFY layoutChanged)
    Q_PROPERTY(int bottomHeight READ bottomHeight WRITE setBottomHeight NOTIFY layoutChanged)
    Q_PROPERTY(bool bottomCollapsed READ bottomCollapsed WRITE setBottomCollapsed NOTIFY layoutChanged)
    Q_PROPERTY(bool bottomUndocked READ bottomUndocked WRITE setBottomUndocked NOTIFY layoutChanged)
    Q_PROPERTY(int bottomTab READ bottomTab WRITE setBottomTab NOTIFY layoutChanged)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY layoutChanged)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY layoutChanged)
    Q_PROPERTY(int windowX READ windowX WRITE setWindowX NOTIFY layoutChanged)
    Q_PROPERTY(int windowY READ windowY WRITE setWindowY NOTIFY layoutChanged)

public:
    explicit LayoutController(const QString& layoutFile, QObject* parent = nullptr);

    int leftWidth() const { return leftWidth_; }
    bool leftCollapsed() const { return leftCollapsed_; }
    bool leftUndocked() const { return leftUndocked_; }
    int rightWidth() const { return rightWidth_; }
    bool rightCollapsed() const { return rightCollapsed_; }
    bool rightUndocked() const { return rightUndocked_; }
    int bottomHeight() const { return bottomHeight_; }
    bool bottomCollapsed() const { return bottomCollapsed_; }
    bool bottomUndocked() const { return bottomUndocked_; }
    int bottomTab() const { return bottomTab_; }
    int windowWidth() const { return windowWidth_; }
    int windowHeight() const { return windowHeight_; }
    int windowX() const { return windowX_; }
    int windowY() const { return windowY_; }

    void setLeftWidth(int v);
    void setLeftCollapsed(bool v);
    void setLeftUndocked(bool v);
    void setRightWidth(int v);
    void setRightCollapsed(bool v);
    void setRightUndocked(bool v);
    void setBottomHeight(int v);
    void setBottomCollapsed(bool v);
    void setBottomUndocked(bool v);
    void setBottomTab(int v);
    void setWindowWidth(int v);
    void setWindowHeight(int v);
    void setWindowX(int v);
    void setWindowY(int v);

    Q_INVOKABLE void load();
    Q_INVOKABLE void save();
    Q_INVOKABLE void restoreDefaults();
    Q_INVOKABLE void toggleLeft();
    Q_INVOKABLE void toggleRight();
    Q_INVOKABLE void toggleBottom();

signals:
    void layoutChanged();
    void layoutRestored();

private:
    QString layoutFile_;
    int leftWidth_ = 260;
    bool leftCollapsed_ = false;
    bool leftUndocked_ = false;
    int rightWidth_ = 320;
    bool rightCollapsed_ = false;
    bool rightUndocked_ = false;
    int bottomHeight_ = 240;
    bool bottomCollapsed_ = false;
    bool bottomUndocked_ = false;
    int bottomTab_ = 0;
    int windowWidth_ = 1480;
    int windowHeight_ = 920;
    int windowX_ = -1;
    int windowY_ = -1;
};

} // namespace trinity::shell
