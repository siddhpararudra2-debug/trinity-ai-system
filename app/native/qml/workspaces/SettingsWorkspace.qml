// Trinity — SETTINGS workspace: 12 categories, live editors
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

SplitView {
    orientation: Qt.Horizontal
    anchors.fill: parent

    // Category list
    Rectangle {
        SplitView.preferredWidth: 200
        SplitView.minimumWidth: 160
        color: TrinityTheme.panel
        border.color: TrinityTheme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8
            Label { text: "CATEGORIES"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 0.8 }
            ListView {
                id: catList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Trinity.settingsCtrl.categories
                currentIndex: 0
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    text: modelData
                    font.pixelSize: 12
                    background: Rectangle {
                        radius: TrinityTheme.radiusM
                        color: highlighted ? TrinityTheme.surface : (hovered ? TrinityTheme.panelRaised : "transparent")
                        border.color: highlighted ? TrinityTheme.borderStrong : "transparent"
                    }
                    onClicked: catList.currentIndex = index
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Restore Layout"
                onClicked: Trinity.layout.restoreDefaults()
            }
        }
    }

    // Editors
    Rectangle {
        SplitView.fillWidth: true
        color: TrinityTheme.bg
        border.color: TrinityTheme.border
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                Label { text: catList.currentIndex>=0 ? catList.model[catList.currentIndex].toUpperCase() : "SETTINGS"; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true }
                Button { text: "Reset Category"; onClicked: Trinity.settingsCtrl.resetCategory(catList.model[catList.currentIndex]) }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ListView {
                    width: parent.width
                    model: catList.currentIndex>=0 ? Trinity.settingsCtrl.categoryEntries(catList.model[catList.currentIndex]) : []
                    spacing: 8
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: row.implicitHeight + 16
                        radius: TrinityTheme.radiusM
                        color: TrinityTheme.panel
                        border.color: TrinityTheme.borderSoft
                        RowLayout {
                            id: row
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label { text: modelData.label; color: TrinityTheme.text; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                Label { text: modelData.key; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: "default: " + modelData.defaultValue; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            }
                            TextField {
                                id: valField
                                Layout.preferredWidth: 220
                                text: modelData.value
                                font.family: TrinityTheme.fontMono
                                font.pixelSize: 11
                                color: TrinityTheme.text
                                background: Rectangle { color: TrinityTheme.surface; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
                                onAccepted: Trinity.settingsCtrl.set(modelData.key, text)
                            }
                            Button { text: "Apply"; onClicked: Trinity.settingsCtrl.set(modelData.key, valField.text) }
                        }
                    }
                }
            }

            // Workspace root quick editor + status
            GroupBox {
                Layout.fillWidth: true
                title: "Workspace"
                background: Rectangle { color: TrinityTheme.surface; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    Label { text: "Root:"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    TextField { id: wsRoot; Layout.fillWidth: true; text: Trinity.settingsCtrl.workspaceRoot(); font.family: TrinityTheme.fontMono; font.pixelSize: 11 }
                    Button { text: "Save"; onClicked: Trinity.settingsCtrl.setWorkspaceRoot(wsRoot.text) }
                }
            }

            Label { text: "Model Provider category is configuration surface only — no network code ships. NullModelProvider is the default (unavailable, honest)."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }
}
