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
        SectionHeader { number: "06"; label: "VISION"; detail: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "no project"; Layout.fillWidth: true }
        ScaffoldBanner { Layout.fillWidth: true; engineId: "vision"; detail: Trinity.workspace.availabilityDetail("vision") }
        GroupBox {
            Layout.fillWidth: true
            title: "Project — Vision"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10; spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.activeProjectId.length>0 ? "Images / video in project" : "No active project"; color: TrinityTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true }
                    Button { text: "Import Image…"; enabled: Trinity.activeProjectId.length>0; onClicked: visionDialog.open() }
                    Button { text: "Refresh"; onClicked: visionList.refresh() }
                }
                ListView {
                    id: visionList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    clip: true
                    model: Trinity.list_project_files("Vision")
                    function refresh(){ model = Trinity.list_project_files("Vision") }
                    delegate: Label { required property var modelData; width: ListView.view.width; text: modelData.name + " · " + modelData.size + " B"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; padding: 4 }
                }
                Label { text: "Future: OpenCV, OCR, object detection, measurement — via Vision engine (ISimulationEngine-like). Files are imported as artifacts (png/jpg) hashed and lineage-tracked."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 200; radius: TrinityTheme.radiusM; color: TrinityTheme.panel; border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 8
                Label { text: "No vision pipelines shipped. The engine boundary exists; execution returns capability_unavailable."; color: TrinityTheme.textMuted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout { Button{ text:"View Pipeline"; enabled:false } Button{ text:"Docs"; onClicked: Qt.openUrlExternally("https://opencode.ai/docs")} }
            }
        }
    }
    FileDialog {
        id: visionDialog
        title: "Import Vision asset"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.bmp *.tiff)", "All files (*)"]
        onAccepted: { Trinity.import_artifact(selectedFile, "vision"); visionList.refresh() }
    }
}
