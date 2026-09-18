// Trinity — properties inspector for the current selection.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#101418"
    border.color: "#222a30"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Label { text: "PROPERTIES"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }

        Label { text: "Selection"; color: "#dfe8ec"; font.bold: true }
        Label { text: "nothing selected"; color: "#5a6a74" }

        Item { Layout.fillHeight: true }

        Label { text: "ENGINES"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }
        Repeater {
            model: Trinity.list_engines()
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: modelData.id + "  v" + modelData.version
                    color: modelData.health === "healthy" ? "#7fdba0" : "#c9a86a"
                    font.pixelSize: 12
                }
                Label {
                    text: modelData.health + (modelData.health_detail ? " — " + modelData.health_detail : "")
                    color: "#5a6a74"; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true
                }
            }
        }
    }
}
