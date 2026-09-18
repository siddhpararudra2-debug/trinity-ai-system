// Trinity — project tree sidebar with workspace domains.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: "#101418"
    border.color: "#222a30"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Label { text: "PROJECT"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }
        ComboBox {
            id: projectSelector
            Layout.fillWidth: true
            model: Trinity.list_projects(true)
            textRole: "name"
            onActivated: function(index) {
                Trinity.open_project(model.get(index).project_id)
            }
        }
        Button {
            Layout.fillWidth: true
            text: qsTr("New Project…")
            onClicked: Trinity.create_project("Project " + new Date().toISOString().substring(0, 10), "")
        }

        Label { text: "DOMAINS"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }
        Repeater {
            model: [
                { label: "CAD", state: "ready" },
                { label: "PCB", state: "scaffolded" },
                { label: "Calculations", state: "ready" },
                { label: "Simulation", state: "scaffolded" },
                { label: "Firmware", state: "scaffolded" },
                { label: "Research", state: "scaffolded" },
                { label: "Artifacts", state: "ready" },
                { label: "Logs", state: "ready" }
            ]
            delegate: ItemDelegate {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.label + (modelData.state === "scaffolded" ? "   (scaffolded)" : "")
                enabled: true
                font.italic: modelData.state === "scaffolded"
            }
        }

        Item { Layout.fillHeight: true }
        Label { text: "data: " + Trinity.dataDir; color: "#3d4a52"; font.pixelSize: 9; elide: Text.ElideMiddle; Layout.fillWidth: true }
    }
}
