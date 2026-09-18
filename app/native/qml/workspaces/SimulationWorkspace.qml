import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"

ScrollView {
    clip: true
    ColumnLayout {
        width: parent.width
        spacing: 16
        anchors.margins: 16
        SectionHeader { number: "05"; label: "SIMULATION"; detail: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "no project"; Layout.fillWidth: true }
        ScaffoldBanner { Layout.fillWidth: true; engineId: "simulation"; detail: Trinity.workspace.availabilityDetail("simulation") }
        GroupBox {
            Layout.fillWidth: true
            title: "Project — Simulation"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.activeProjectId.length>0 ? "Simulation files in project" : "No active project"; color: TrinityTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true }
                    Button { text: "Import Model…"; enabled: Trinity.activeProjectId.length>0; onClicked: simDialog.open() }
                    Button { text: "Refresh"; onClicked: simList.refresh() }
                }
                ListView {
                    id: simList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    clip: true
                    model: Trinity.list_project_files("Simulation")
                    function refresh(){ model = Trinity.list_project_files("Simulation") }
                    delegate: Label { required property var modelData; width: ListView.view.width; text: modelData.name + " · " + modelData.size + " B"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; padding: 4 }
                }
                Label { text: "ISimulationEngine: loadModel/configure/run/pause/stop/results — WorkflowRunner DAG orders jobs. Native simulation not bundled; use workflow to chain CAD → sim placeholder."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 200
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                Label { text: "No solvers are bundled. Workflows DAG is available (native)."; color: TrinityTheme.textMuted; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label { text: "Planned: structural / thermal / CFD adapters via ICADAdapter-like boundary. The WorkflowRunner (topological order over JobSystem) is the execution substrate."; color: TrinityTheme.textFaint; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout { Button{ text:"View Jobs"; onClicked: Trinity.workspace.switchTo("jobs")} Button{ text:"Simulate (disabled)"; enabled:false } Button{ text:"Run Workflow"; onClicked: Trinity.workspace.switchTo("jobs")} }
            }
        }
    }
    FileDialog {
        id: simDialog
        title: "Import Simulation model"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Model (*.json *.stl *.obj *.step)", "All files (*)"]
        onAccepted: { Trinity.import_artifact(selectedFile, "simulation"); simList.refresh() }
    }
}
