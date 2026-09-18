// Trinity — PCB workspace (scaffolded engine, real file import & project structure)
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
        SectionHeader { number: "04"; label: "PCB — LAYOUT"; detail: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "no project"; Layout.fillWidth: true }
        ScaffoldBanner { Layout.fillWidth: true; engineId: "pcb"; detail: Trinity.workspace.availabilityDetail("pcb") }

        // Project PCB folder
        GroupBox {
            Layout.fillWidth: true
            title: "Project — PCB"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.activeProjectId.length>0 ? ("PCB files in project " + Trinity.activeProjectId.substring(0,8)) : "No active project — create one in Projects."; color: TrinityTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Button { text: "Import File…"; enabled: Trinity.activeProjectId.length>0; onClicked: pcbImportDialog.open() }
                    Button { text: "Refresh"; onClicked: pcbFiles.refresh() }
                }
                ListView {
                    id: pcbFiles
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(140, count*28 + 8)
                    clip: true
                    model: Trinity.list_project_files("PCB")
                    function refresh() { model = Trinity.list_project_files("PCB") }
                    delegate: Label {
                        required property var modelData
                        width: ListView.view.width
                        text: modelData.name + "  ·  " + modelData.size + " B"
                        color: TrinityTheme.textFaint
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 10
                        padding: 4
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
                Label { text: "Path: <project>/PCB/ — created on New Project (see ProjectStore 8 subfolders). Import uses native file dialog and copies into artifact store (type pcb) — offline, validated, hashed."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                width: 420
                Label { Layout.alignment: Qt.AlignHCenter; text: "▭"; color: TrinityTheme.textDim; font.pixelSize: 42 }
                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "PCB generation is scaffolded (501)."; color: TrinityTheme.textMuted; font.pixelSize: 13; wrapMode: Text.Wrap }
                Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: "Netlist import, placement, routing and Gerber export return CapabilityUnavailableError. The registry reports health: scaffolded. Import artifact path works today."; color: TrinityTheme.textFaint; font.pixelSize: 11; wrapMode: Text.Wrap }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Button { text: "View Registry"; onClicked: Trinity.workspace.switchTo("cad") }
                    Button { text: "Generate (disabled)"; enabled: false; ToolTip.text: "SCAFFOLD — 501"; ToolTip.visible: hovered }
                }
            }
        }
        GroupBox {
            Layout.fillWidth: true
            title: "Planned capabilities"
            background: Rectangle { color: "transparent"; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            Flow {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Repeater {
                    model: ["netlist import","placement","routing","DRC","Gerber","BOM"]
                    Label { required property string modelData; text: modelData; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; padding: 6; background: Rectangle{color: TrinityTheme.surface; border.color: TrinityTheme.borderSoft; radius: 3} }
                }
            }
        }
    }

    FileDialog {
        id: pcbImportDialog
        title: "Import PCB file"
        fileMode: FileDialog.OpenFile
        nameFilters: ["PCB/Netlist (*.kicad_pcb *.kicad_sch *.brd *.net *.json)", "All files (*)"]
        onAccepted: {
            const res = Trinity.import_artifact(selectedFile, "pcb")
            pcbFiles.refresh()
        }
    }
}
