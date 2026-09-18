// Trinity — main desktop window: engineering workstation layout.
// Sidebar (project tree) | 3D viewport (center) | Properties (right)
// Command console above the bottom dock (Jobs/Logs/Artifacts/Validation).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1400
    height: 900
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: qsTr("TRINITY — Engineering Operating System")
    color: "#14171a"

    menuBar: MenuBar {
        Menu { title: qsTr("File"); MenuItem { text: qsTr("New Project…"); onTriggered: console.log("new project") }
                 MenuItem { text: qsTr("Open Project…") } MenuItem { text: qsTr("Import…") }
                 MenuItem { text: qsTr("Export…") } MenuSeparator {}
                 MenuItem { text: qsTr("Quit"); role: MenuItem.QuitRole } }
        Menu { title: qsTr("Edit"); MenuItem { text: qsTr("Undo") } MenuItem { text: qsTr("Redo") } }
        Menu { title: qsTr("View"); MenuItem { text: qsTr("Command Palette\tCtrl+K"); onTriggered: palette.open() } }
        Menu { title: qsTr("Project"); MenuItem { text: qsTr("Archive") } }
        Menu { title: qsTr("Tools"); MenuItem { text: qsTr("Settings…") ; onTriggered: settingsDialog.open() }
                 MenuItem { text: qsTr("Job Monitor") } }
        Menu { title: qsTr("Help"); MenuItem { text: qsTr("About Trinity") } }
    }

    Shortcut { sequence: "Ctrl+K"; onActivated: palette.open() }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: "TRINITY"; font.bold: true; font.letterSpacing: 2; color: "#4fd6e5" }
            Label { text: Trinity.activeProjectId ? "project: " + Trinity.activeProjectId.substring(0, 8)
                                                   : "no project"; color: "#8a9aa4" }
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Run Command"); onClicked: palette.open() }
        }
    }

    // ---------------------------------------------------------------- body
    RowLayout {
        anchors.fill: parent
        spacing: 4

        ProjectTree {
            Layout.fillHeight: true
            Layout.preferredWidth: 240
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4

            Viewport3D {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            CommandConsole {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                onCommandIssued: function(text) {
                    const result = Trinity.run_command(text)
                    resultBox.text = result.ok
                        ? "job " + result.job_id + " → " + result.status
                        : "error: " + result.error
                }
                Label { id: resultBox; visible: false }
            }

            JobMonitor {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
            }
        }

        PropertiesPanel {
            Layout.fillHeight: true
            Layout.preferredWidth: 280
        }
    }

    // ------------------------------------------------------------ overlays
    CommandPalette { id: palette }
    SettingsDialog { id: settingsDialog }
}
