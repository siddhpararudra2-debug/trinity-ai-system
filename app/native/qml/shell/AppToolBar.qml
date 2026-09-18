// Trinity — top toolbar: breadcrumb, actions, status
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ToolBar {
    id: root
    height: TrinityTheme.topBarHeight
    background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; border.width: 0; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: TrinityTheme.border } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        // Brand
        Rectangle {
            width: 28; height: 28; radius: 3
            color: TrinityTheme.text; border.color: TrinityTheme.text
            Label { anchors.centerIn: parent; text: "T"; color: TrinityTheme.bg; font.bold: true; font.pixelSize: 13 }
        }
        Label { text: "TRINITY"; color: TrinityTheme.text; font.bold: true; font.letterSpacing: 1.5; font.pixelSize: 12; font.family: TrinityTheme.fontMono }
        Label { text: "·"; color: TrinityTheme.textDim }
        Label {
            text: Trinity.workspace.currentWorkspaceTitle.toUpperCase()
            color: TrinityTheme.accentBright
            font.family: TrinityTheme.fontMono
            font.pixelSize: 10
            font.letterSpacing: 1.0
        }
        Label {
            visible: Trinity.activeProjectId.length > 0
            text: "— " + Trinity.activeProjectId.substring(0,8)
            color: TrinityTheme.textFaint
            font.family: TrinityTheme.fontMono
            font.pixelSize: 10
        }
        Label {
            visible: Trinity.activeProjectId.length === 0
            text: "— no project"
            color: TrinityTheme.textDim
            font.family: TrinityTheme.fontMono
            font.pixelSize: 10
            font.italic: true
        }

        Item { Layout.fillWidth: true }

        // Actions
        RowLayout { spacing: 6
            Button {
                text: "Run (F5)"
                enabled: Trinity.workspace.isAvailable(Trinity.workspace.currentWorkspaceId)
                onClicked: root.runRequested()
                ToolTip.text: "F5 — submit current command"
                ToolTip.visible: hovered
            }
            Button {
                text: "Validate (F6)"
                onClicked: root.validateRequested()
                ToolTip.text: "F6 — validate selected artifact"
                ToolTip.visible: hovered
            }
            Button {
                text: "Export"
                onClicked: root.exportRequested()
                ToolTip.text: "Ctrl+Shift+S"
                ToolTip.visible: hovered
            }
            ToolSeparator {}
            Button {
                text: "⌘ K"
                onClicked: root.paletteRequested()
                ToolTip.text: "Ctrl+K — command palette"
                ToolTip.visible: hovered
            }
        }

        // Status
        RowLayout { spacing: 8
            Rectangle { width: 6; height: 6; radius: 3; color: TrinityTheme.success }
            Label { text: Trinity.jobModel.activeCount > 0 ? (Trinity.jobModel.activeCount + " running") : "idle"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            Label { text: "·"; color: TrinityTheme.textDim }
            Label { text: Trinity.engineModel.count + " engines"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
        }
    }

    signal runRequested()
    signal validateRequested()
    signal exportRequested()
    signal paletteRequested()
}
