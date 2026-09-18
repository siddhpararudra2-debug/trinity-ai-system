// Trinity — CAD workspace: IR form + viewport + actions
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"
import "../viewport"

ColumnLayout {
    spacing: 12
    anchors.fill: parent
    anchors.margins: 12

    SectionHeader { number: "02"; label: "CAD — QUADCOPTER FRAME"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 12

        // Left — IR parameters
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                Label { text: "PARAMETERS — IR"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 0.8 }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 10; rowSpacing: 8
                    Label { text: "Overall size (mm)"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    SpinBox { id: overallSize; from: 20; to: 400; value: 50; editable: true; Layout.fillWidth: true }
                    Label { text: "Center plate (mm)"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    SpinBox { id: centerPlate; from: 10; to: 120; value: 21; editable: true; Layout.fillWidth: true }
                    Label { text: "Arm width (mm)"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    SpinBox { id: armWidth; from: 2; to: 30; value: 5; editable: true; Layout.fillWidth: true }
                    Label { text: "Arm thickness (mm)"; color: TrinityTheme.textMuted; font.pixelSize: 11 }
                    SpinBox { id: armThick; from: 1; to: 20; value: 3; editable: true; Layout.fillWidth: true }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: TrinityTheme.borderSoft }
                Label { text: "Engines: CAD (healthy) validates dimensions, finite vertices, degenerate tris, FDM min feature. STEP export is scaffolded."; color: TrinityTheme.textFaint; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true; lineHeight: 1.35 }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        Layout.fillWidth: true
                        text: "Generate — 50 mm quadcopter frame  (F5)"
                        highlighted: true
                        enabled: Trinity.activeProjectId.length>0 && Trinity.workspace.isAvailable("cad")
                        onClicked: {
                            const txt = `create a ${overallSize.value} mm quadcopter frame`;
                            const r = Trinity.run_command_async(txt);
                            statusLabel.text = r.ok ? ("queued " + r.job_id.substring(0,8)) : ("✗ " + r.error);
                        }
                        ToolTip.text: !Trinity.workspace.isAvailable("cad") ? "CAD unavailable" : (Trinity.activeProjectId.length===0 ? "Create/open a project first" : "F5")
                        ToolTip.visible: hovered
                    }
                    Button {
                        Layout.fillWidth: true
                        text: "Generate (current params)"
                        onClicked: {
                            const txt = `create a ${overallSize.value} mm quadcopter frame`;
                            Trinity.run_command_async(txt);
                        }
                    }
                    Label { id: statusLabel; Layout.fillWidth: true; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; wrapMode: Text.Wrap }
                }
                Item { Layout.fillHeight: true }
                // Validation hint
                Rectangle {
                    Layout.fillWidth: true
                    radius: TrinityTheme.radiusM
                    color: TrinityTheme.surface
                    border.color: TrinityTheme.borderSoft
                    implicitHeight: hintCol.implicitHeight + 16
                    ColumnLayout {
                        id: hintCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        Label { text: "OUTPUTS"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8 }
                        Label { text: "STL (binary, 80-byte header) + preview JSON — byte-compatible with backend. GLB via Python host over IPC."; color: TrinityTheme.textMuted; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
            }
        }

        // Center — viewport
        View3D {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
