// Trinity — left navigation / project panel (collapsible, dockable)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    color: TrinityTheme.panel
    border.color: TrinityTheme.border

    signal newProjectRequested()
    function requestNewProject() { newDialog.open() }
    Component.onCompleted: newProjectRequested.connect(requestNewProject)

    // Expose for SplitView handle / collapse animation
    property bool collapsed: Trinity.layout.leftCollapsed
    property bool undocked: Trinity.layout.leftUndocked

    // Smooth collapse
    Behavior on width { NumberAnimation { duration: TrinityTheme.durationNormal; easing.type: Easing.InOutCubic } }
    Behavior on opacity { NumberAnimation { duration: TrinityTheme.durationFast } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        opacity: collapsed ? 0 : 1
        visible: !collapsed
        enabled: !collapsed

        // Header with collapse/undock
        RowLayout {
            Layout.fillWidth: true
            Label { text: "NAVIGATOR"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 1.2; Layout.fillWidth: true }
            ToolButton { text: "◫"; font.pixelSize: 11; onClicked: Trinity.layout.leftUndocked = !Trinity.layout.leftUndocked; ToolTip.text: Trinity.layout.leftUndocked ? "Dock" : "Undock"; ToolTip.visible: hovered }
            ToolButton { text: "‹"; font.pixelSize: 12; onClicked: Trinity.layout.toggleLeft(); ToolTip.text: "Collapse"; ToolTip.visible: hovered }
        }

        // Project selector
        GroupBox {
            Layout.fillWidth: true
            title: "PROJECT"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 1.0; leftPadding: 4 }
            background: Rectangle { color: "transparent"; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                ComboBox {
                    id: projectCombo
                    Layout.fillWidth: true
                    model: Trinity.projectModel
                    textRole: "name"
                    displayText: currentIndex >=0 ? Trinity.projectModel.get(currentIndex).name : (Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "No project")
                    onActivated: function(idx) {
                        const m = Trinity.projectModel.get(idx);
                        Trinity.projectModel.openProject(m.projectId);
                    }
                    Component.onCompleted: Trinity.projectModel.refresh(true)
                    delegate: ItemDelegate {
                        width: ListView.view.width
                        text: model.name + (model.archived ? "  [archived]" : "")
                        highlighted: ListView.isCurrentItem
                        font.family: TrinityTheme.fontSans
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        Layout.fillWidth: true
                        text: "New"
                        onClicked: newDialog.open()
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Open"
                        onClicked: Trinity.workspace.switchTo("projects")
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId : "No active project — create or open one."
                    color: Trinity.activeProjectId.length>0 ? TrinityTheme.textFaint : TrinityTheme.textDim
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                    wrapMode: Text.Wrap
                }
            }
        }

        // Workspaces
        Label { text: "WORKSPACES"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 1.0 }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: Trinity.workspace.workspaceList()
            spacing: 2
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 36
                highlighted: Trinity.workspace.currentWorkspace === index
                background: Rectangle {
                    radius: TrinityTheme.radiusM
                    color: parent.highlighted ? TrinityTheme.surface : (parent.hovered ? TrinityTheme.panelRaised : "transparent")
                    border.color: parent.highlighted ? TrinityTheme.borderStrong : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 10
                    Label {
                        Layout.leftMargin: 10
                        text: {
                            const map = {"home":"⌂","projects":"▦","cad":"⬢","pcb":"▭","math":"∑","simulation":"◈","vision":"◎","firmware":"⬣","research":"⬔","artifacts":"⬚","jobs":"▤","settings":"⚙"};
                            return map[modelData.id] || "·";
                        }
                        color: highlighted ? TrinityTheme.text : (modelData.available ? TrinityTheme.textMuted : TrinityTheme.textDim)
                        font.pixelSize: 13
                    }
                    Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        color: highlighted ? TrinityTheme.text : (modelData.available ? TrinityTheme.text : TrinityTheme.textFaint)
                        font.pixelSize: 12
                        font.bold: highlighted
                    }
                    StatusPill {
                        visible: !modelData.available
                        status: modelData.badge
                        variant: "scaffold"
                    }
                    Label {
                        visible: modelData.available && Trinity.workspace.currentWorkspace === index
                        text: "●"
                        color: TrinityTheme.accentBright
                        font.pixelSize: 7
                        Layout.rightMargin: 10
                    }
                }
                onClicked: Trinity.workspace.setCurrentWorkspace(index)
                ToolTip.text: modelData.available ? modelData.title : modelData.detail
                ToolTip.visible: hovered && !modelData.available
                enabled: true
                opacity: modelData.available ? 1.0 : 0.85
            }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }

        // Footer meta
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }
            Label { text: Trinity.dataDir; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 8; elide: Text.ElideMiddle; Layout.fillWidth: true }
            RowLayout {
                Label { text: Trinity.engineModel.count + " engines"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Label { text: Trinity.artifactModel.count + " artifacts"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
            }
        }
    }

    // Collapsed strip
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12
        visible: collapsed
        opacity: collapsed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: TrinityTheme.durationFast } }
        ToolButton { text: "›"; font.pixelSize: 14; onClicked: Trinity.layout.toggleLeft() }
        Repeater {
            model: 12
            ToolButton {
                required property int index
                text: ["⌂","▦","⬢","▭","∑","◈","◎","⬣","⬔","⬚","▤","⚙"][index]
                highlighted: Trinity.workspace.currentWorkspace === index
                onClicked: { Trinity.layout.leftCollapsed = false; Trinity.workspace.setCurrentWorkspace(index); }
                font.pixelSize: 12
                width: 32; height: 28
            }
        }
    }

    // New project dialog
    Dialog {
        id: newDialog
        title: "New Project"
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        modal: true
        width: 420
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusL }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "Name"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            TextField { id: newName; placeholderText: "Project name"; Layout.fillWidth: true; font.pixelSize: 13 }
            Label { text: "Description (optional)"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            TextField { id: newDesc; placeholderText: "What are you building?"; Layout.fillWidth: true }
        }
        onAccepted: {
            if (newName.text.trim().length === 0) return;
            Trinity.projectModel.createProject(newName.text.trim(), newDesc.text.trim());
            newName.text = ""; newDesc.text = "";
        }
    }
}
