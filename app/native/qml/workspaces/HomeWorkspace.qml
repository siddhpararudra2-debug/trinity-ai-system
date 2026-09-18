// Trinity — HOME workspace: system overview
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ScrollView {
    clip: true
    ColumnLayout {
        width: parent.width
        spacing: 16
        anchors.margins: 16

        SectionHeader { number: "00"; label: "HOME — SYSTEM OVERVIEW"; Layout.fillWidth: true }

        Label {
            Layout.fillWidth: true
            text: "Engineering Operating System — deterministic execution, independent verification."
            color: TrinityTheme.text
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.Wrap
        }
        Label {
            Layout.fillWidth: true
            text: "Central status plane. Every action flows: Command → Job → Engine → Validation → Artifact. No probabilistic execution in the critical path."
            color: TrinityTheme.textMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            lineHeight: 1.5
        }

        // Metrics
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 12; rowSpacing: 12
            Repeater {
                model: [
                    { label:"Projects", value: Trinity.projectModel.count, sub:"datasets" },
                    { label:"Jobs", value: Trinity.jobModel.count, sub: Trinity.jobModel.activeCount + " active" },
                    { label:"Artifacts", value: Trinity.artifactModel.count, sub:"verified lineage" },
                    { label:"Engines", value: Trinity.engineModel.count, sub:"registry" }
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 96
                    radius: TrinityTheme.radiusL
                    color: TrinityTheme.surface
                    border.color: TrinityTheme.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        Label { text: modelData.value; color: TrinityTheme.accentBright; font.pixelSize: 28; font.bold: true; font.family: TrinityTheme.fontDisplay }
                        Label { text: modelData.label; color: TrinityTheme.text; font.pixelSize: 12; font.family: TrinityTheme.fontMono; font.letterSpacing: 0.6 }
                        Label { text: modelData.sub; color: TrinityTheme.textFaint; font.pixelSize: 10 }
                    }
                }
            }
        }

        // Engine strip
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: Trinity.engineModel
                Rectangle {
                    required property var model
                    Layout.fillWidth: true
                    implicitHeight: 54
                    radius: TrinityTheme.radiusM
                    color: model.health==="healthy" ? TrinityTheme.successBg : (model.health==="scaffolded" ? TrinityTheme.scaffoldBg : TrinityTheme.surface)
                    border.color: model.health==="healthy" ? "#1E3A28" : (model.health==="scaffolded" ? "#3A2E14" : TrinityTheme.border)
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        Label { text: model.engineId.toUpperCase(); color: model.health==="healthy" ? "#7FD8A0" : (model.health==="scaffolded" ? "#C9B07A" : TrinityTheme.text); font.family: TrinityTheme.fontMono; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true }
                        Label { text: model.health.toUpperCase() + (model.healthDetail.length>0 ? " · " + model.healthDetail.substring(0,48) : ""); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                }
            }
        }

        // Recent jobs
        GroupBox {
            Layout.fillWidth: true
            title: "Recent Jobs"
            label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; leftPadding: 6 }
            background: Rectangle { color: "transparent"; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                Repeater {
                    model: Math.min(4, Trinity.jobModel.count)
                    Rectangle {
                        required property int index
                        Layout.fillWidth: true
                        height: 38
                        radius: TrinityTheme.radiusM
                        color: TrinityTheme.panelRaised
                        border.color: TrinityTheme.borderSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            Label { text: Trinity.jobModel.get(index).jobId.substring(0,8); color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.preferredWidth: 80 }
                            Label { text: Trinity.jobModel.get(index).status; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.preferredWidth: 90 }
                            Label { text: Trinity.jobModel.get(index).engine + "." + Trinity.jobModel.get(index).operation; color: TrinityTheme.textFaint; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
                Label { visible: Trinity.jobModel.count===0; text: "No jobs yet — dispatch a command via Console or CAD."; color: TrinityTheme.textDim; font.pixelSize: 11; Layout.fillWidth: true }
                Button { text: "Go to Jobs →"; onClicked: Trinity.workspace.switchTo("jobs") }
            }
        }

        // Quick actions
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Button { text: "New Project (Ctrl+N)"; onClicked: Trinity.workspace.switchTo("projects"); highlighted: true }
            Button { text: "Generate CAD"; onClicked: Trinity.workspace.switchTo("cad") }
            Button { text: "Open Palette (Ctrl+K)"; onClicked: palette.open() }
            Item { Layout.fillWidth: true }
        }
    }
}
