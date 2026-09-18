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
        SectionHeader { number: "07"; label: "FIRMWARE"; detail: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "no project"; Layout.fillWidth: true }
        ScaffoldBanner { Layout.fillWidth: true; engineId: "firmware"; detail: Trinity.workspace.availabilityDetail("firmware") }

        GroupBox {
            Layout.fillWidth: true
            title: "Project — Firmware"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.activeProjectId.length>0 ? "Firmware files in project " + Trinity.activeProjectId.substring(0,8) : "No active project"; color: TrinityTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true }
                    Button { text: "Import Source…"; enabled: Trinity.activeProjectId.length>0; onClicked: fwDialog.open() }
                    Button { text: "Refresh"; onClicked: fwList.refresh() }
                }
                ListView {
                    id: fwList
                    Layout.fillWidth: true
                    Layout.preferredHeight: 100
                    clip: true
                    model: Trinity.list_project_files("Firmware")
                    function refresh(){ model = Trinity.list_project_files("Firmware") }
                    delegate: Label { required property var modelData; width: ListView.view.width; text: modelData.name + " · " + modelData.size + " B"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; padding: 4 }
                }
                Label { text: "Supports: C/C++, PlatformIO, source files, build output, logs. Build/Flash remain scaffolded (toolchains not bundled)."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 220; radius: TrinityTheme.radiusM; color: TrinityTheme.panel; border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 10
                Label { text: "Toolchains (arm-none-eabi, avrdude, openocd) are not bundled. Firmware engine is scaffolded."; color: TrinityTheme.textMuted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout { Button{ text:"Build (disabled)"; enabled:false } Button{ text:"Flash (disabled)"; enabled:false } Item{Layout.fillWidth:true} StatusPill{ status:"SCAFFOLD"; variant:"scaffold"} }
            }
        }
    }
    FileDialog {
        id: fwDialog
        title: "Import Firmware source"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Source (*.c *.cpp *.h *.ino *.py)", "All files (*)"]
        onAccepted: { Trinity.import_artifact(selectedFile, "firmware"); fwList.refresh() }
    }
}
