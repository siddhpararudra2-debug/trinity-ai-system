// Trinity — bottom dock: Jobs / Logs / Artifacts / Validation / Output.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: "#101418"
    border.color: "#222a30"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: qsTr("Jobs") }
            TabButton { text: qsTr("Logs") }
            TabButton { text: qsTr("Artifacts") }
            TabButton { text: qsTr("Validation") }
            TabButton { text: qsTr("Output") }
        }

        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            ListView {
                model: Trinity.recent_jobs(50)
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.job_id.substring(0, 8) + "  " + modelData.status + "  "
                          + modelData.engine + "." + modelData.operation
                }
            }

            TextArea { readOnly: true; color: "#9fb3bd"; font.family: "Consolas"; text: "log stream attaches to job_log signal" }

            ListView {
                model: Trinity.activeProjectId ? Trinity.project_artifacts() : []
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    text: modelData.type + "  " + modelData.filename + "  [" + modelData.state + "]"
                }
            }

            TextArea { readOnly: true; color: "#9fb3bd"; text: "validation history renders per artifact" }
            TextArea { readOnly: true; color: "#9fb3bd"; font.family: "Consolas"; text: "structured output" }
        }
    }
}
