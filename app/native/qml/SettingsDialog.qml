// Trinity — settings dialog with the full brief category set.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: settingsDialog
    title: qsTr("Settings")
    modal: true
    width: 720
    height: 520
    parent: Overlay.overlay
    anchors.centerIn: parent
    standardButtons: Dialog.Close
    background: Rectangle { color: "#141a1f"; border.color: "#2a343b" }

    // Category model mirrors the native SettingsStore descriptors; the Model
    // Provider section is configuration surface only (no network code ships).
    property var categories: ["General", "Appearance", "Workspace", "Engines", "CAD",
                              "Rendering", "Performance", "Python", "Plugins",
                              "Model Provider", "Logging", "Updates"]

    contentItem: RowLayout {
        spacing: 8
        ListView {
            Layout.preferredWidth: 170
            Layout.fillHeight: true
            model: settingsDialog.categories
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: stack.currentIndex = index
            }
        }
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            Repeater {
                model: settingsDialog.categories
                delegate: TextArea {
                    required property int index
                    readOnly: true
                    wrapMode: Text.Wrap
                    color: "#9fb3bd"
                    text: {
                        const group = Trinity.all_settings()[settingsDialog.categories[index]]
                        if (!group) return "no settings in this group yet"
                        let out = ""
                        for (let i = 0; i < group.length; ++i) {
                            out += group[i].label + "\n    " + group[i].value + "\n"
                        }
                        return out
                    }
                }
            }
        }
    }
}
