// Trinity — global command palette (Ctrl+K), upgraded.
// Filterable, keyboard-navigable, executes via CommandModel (registry + shell routing).
// All execution delegates to C++ — QML only dispatches.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "./theme"

Popup {
    id: palette
    width: 640
    height: 460
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    parent: Overlay.overlay
    x: (parent.width - width)/2
    y: Math.max(80, (parent.height - height)/3)
    padding: 0
    background: Rectangle {
        color: TrinityTheme.panel
        border.color: TrinityTheme.borderStrong
        radius: TrinityTheme.radiusL
        // subtle shadow via border + overlay dim implied by modal
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0,0,0,0.48) }

    property alias filterText: filter.text

    function openPalette() {
        filter.text = "";
        open();
        filter.forceActiveFocus();
    }

    onOpened: {
        filter.forceActiveFocus();
        filter.selectAll();
    }
    onClosed: filter.text = ""

    contentItem: ColumnLayout {
        spacing: 0

        // Search field
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            radius: TrinityTheme.radiusL
            color: TrinityTheme.surface
            border.color: TrinityTheme.border
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 10
                Label { text: "›"; color: TrinityTheme.accentBright; font.bold: true; font.pixelSize: 16 }
                TextField {
                    id: filter
                    Layout.fillWidth: true
                    placeholderText: "Type a command…  (e.g. new project, cad generate, math, validate)"
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 13
                    color: TrinityTheme.text
                    background: Rectangle { color: "transparent" }
                    onTextChanged: Trinity.commandFilterModel.filterText = text
                    Keys.onDownPressed: list.incrementCurrentIndex()
                    Keys.onUpPressed: list.decrementCurrentIndex()
                    Keys.onReturnPressed: executeCurrent()
                    Keys.onEnterPressed: executeCurrent()
                    Keys.onEscapePressed: palette.close()
                }
                Label {
                    text: "ESC"
                    color: TrinityTheme.textFaint
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 9
                    padding: 6
                    background: Rectangle { color: TrinityTheme.bg; border.color: TrinityTheme.borderSoft; radius: 3 }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }

        // Results
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            clip: true
            model: Trinity.commandFilterModel
            currentIndex: 0
            spacing: 2
            highlight: Rectangle { color: TrinityTheme.surface; border.color: TrinityTheme.borderStrong; radius: TrinityTheme.radiusM }
            highlightMoveDuration: TrinityTheme.durationFast
            delegate: ItemDelegate {
                required property var model
                required property int index
                width: ListView.view.width
                height: 44
                highlighted: ListView.isCurrentItem
                background: Rectangle {
                    radius: TrinityTheme.radiusM
                    color: highlighted ? TrinityTheme.surface : (hovered ? TrinityTheme.panelRaised : "transparent")
                    border.color: highlighted ? TrinityTheme.borderStrong : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 12
                    Label {
                        Layout.leftMargin: 10
                        text: model.category.substring(0,1)
                        color: TrinityTheme.textFaint
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 9
                        Layout.preferredWidth: 14
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: model.title; color: TrinityTheme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: model.category + " · " + model.commandId; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    Label {
                        text: "↵"
                        color: ListView.isCurrentItem ? TrinityTheme.accentBright : TrinityTheme.textDim
                        font.pixelSize: 12
                        Layout.rightMargin: 10
                    }
                }
                onClicked: execute(index)
            }
            ScrollBar.vertical: ScrollBar {}
            Label {
                anchors.centerIn: parent
                visible: list.count === 0
                text: "No commands match \"" + filter.text + "\""
                color: TrinityTheme.textDim
                font.pixelSize: 12
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }

        // Footer hints
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 12
            Label { text: "↑↓ navigate  ·  ↵ execute  ·  Esc close"; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }
            Label { text: list.count + " commands"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
        }
    }

    function execute(idx) {
        const m = Trinity.commandFilterModel.get(idx);
        if (!m || !m.commandId) return;
        const id = m.commandId;
        // Route special ids to workspace/navigation or engine actions
        if (id === "project.new" || id === "palette.open" || id === "workspace.new_project") {
            // New project is handled via Projects workspace; open dialog via workspace switch and let LeftPanel dialog handle
            Trinity.workspace.switchTo("projects");
            palette.close();
            return;
        }
        if (id === "project.open") { Trinity.workspace.switchTo("projects"); palette.close(); return; }
        if (id === "project.import" || id === "project.export") { Trinity.workspace.switchTo("projects"); palette.close(); return; }
        if (id === "cad.generate") { Trinity.workspace.switchTo("cad"); Trinity.run_command_async("create a 50 mm quadcopter frame"); palette.close(); return; }
        if (id === "math.evaluate") { Trinity.workspace.switchTo("math"); palette.close(); return; }
        if (id === "artifact.validate") { Trinity.workspace.switchTo("artifacts"); palette.close(); return; }
        if (id === "view.jobs") { Trinity.workspace.switchTo("jobs"); palette.close(); return; }
        if (id === "view.artifacts") { Trinity.workspace.switchTo("artifacts"); palette.close(); return; }
        if (id === "settings.open") { Trinity.workspace.switchTo("settings"); palette.close(); return; }
        if (id.startsWith("view.") || id.startsWith("palette.")) { palette.close(); return; }

        // Otherwise delegate to C++ registry
        Trinity.commandModel.execute(id, {});
        palette.close();
    }
    function executeCurrent() {
        if (list.count===0) return;
        execute(list.currentIndex);
    }
}
