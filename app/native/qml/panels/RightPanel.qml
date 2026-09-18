// Trinity — right inspector / properties panel
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    color: TrinityTheme.panel
    border.color: TrinityTheme.border

    property bool collapsed: Trinity.layout.rightCollapsed
    Behavior on width { NumberAnimation { duration: TrinityTheme.durationNormal; easing.type: Easing.InOutCubic } }

    // Tabs: Properties | Engines | Selection
    property int currentTab: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10
        opacity: collapsed ? 0 : 1
        visible: !collapsed

        RowLayout {
            Layout.fillWidth: true
            Label { text: "INSPECTOR"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 1.2; Layout.fillWidth: true }
            ToolButton { text: "◫"; font.pixelSize: 11; onClicked: Trinity.layout.rightUndocked = !Trinity.layout.rightUndocked; ToolTip.text: Trinity.layout.rightUndocked ? "Dock" : "Undock"; ToolTip.visible: hovered }
            ToolButton { text: "›"; font.pixelSize: 12; onClicked: Trinity.layout.toggleRight(); ToolTip.text: "Collapse"; ToolTip.visible: hovered }
        }

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            currentIndex: currentTab
            onCurrentIndexChanged: currentTab = currentIndex
            TabButton { text: "Props"; font.pixelSize: 11; font.family: TrinityTheme.fontMono }
            TabButton { text: "Engines"; font.pixelSize: 11; font.family: TrinityTheme.fontMono }
            TabButton { text: "Detail"; font.pixelSize: 11; font.family: TrinityTheme.fontMono }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentTab

            // 0 — Properties
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    // Active project
                    GroupBox {
                        Layout.fillWidth: true
                        title: "Active Project"
                        label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; leftPadding: 4 }
                        background: Rectangle { color: TrinityTheme.surface; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Label { text: Trinity.activeProjectId.length>0 ? Trinity.activeProjectId : "— none —"; color: Trinity.activeProjectId.length>0 ? TrinityTheme.text : TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                            Label { visible: Trinity.activeProjectId.length>0; text: Trinity.projectModel.count + " projects · " + Trinity.artifactModel.count + " artifacts"; color: TrinityTheme.textFaint; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { visible: Trinity.activeProjectId.length===0; text: "Create or open a project to attach jobs and artifacts."; color: TrinityTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        }
                    }

                    // Viewport controls (when in CAD)
                    GroupBox {
                        Layout.fillWidth: true
                        title: "Viewport"
                        label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; leftPadding: 4 }
                        background: Rectangle { color: "transparent"; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                        GridLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            columns: 2
                            columnSpacing: 10; rowSpacing: 6
                            Label { text: "Preset"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            ComboBox { Layout.fillWidth: true; model: ["ISO","FRONT","TOP","RIGHT","FIT"]; currentIndex: ["ISO","FRONT","TOP","RIGHT","FIT"].indexOf(Trinity.viewport.viewPreset); onActivated: function(i){ Trinity.viewport.setView(model[i]) } }
                            Label { text: "Projection"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Ortho"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
                                Switch { checked: Trinity.viewport.ortho; onToggled: Trinity.viewport.ortho = checked }
                                Item { Layout.fillWidth: true }
                            }
                            Label { text: "Wireframe"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            Switch { checked: Trinity.viewport.wireframe; onToggled: Trinity.viewport.wireframe = checked }
                            Label { text: "Grid"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            Switch { checked: Trinity.viewport.showGrid; onToggled: Trinity.viewport.showGrid = checked }
                            Label { text: "Axes"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            Switch { checked: Trinity.viewport.showAxes; onToggled: Trinity.viewport.showAxes = checked }
                            Label { text: "Spin"; color: TrinityTheme.textFaint; font.pixelSize: 11 }
                            Switch { checked: Trinity.viewport.spinning; onToggled: Trinity.viewport.spinning = checked }
                        }
                    }

                    // Mesh stats
                    GroupBox {
                        Layout.fillWidth: true
                        title: "Geometry"
                        label: Label { text: parent.title; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; leftPadding: 4 }
                        background: Rectangle { color: TrinityTheme.surface; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Label { text: "Status: " + Trinity.viewport.statusText; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            RowLayout { Layout.fillWidth: true; Label{ text:"Triangles"; color: TrinityTheme.textFaint; font.pixelSize: 11; Layout.fillWidth:true} Label{ text: Trinity.viewport.meshStats.triangles || 0; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11 } }
                            RowLayout { Layout.fillWidth: true; Label{ text:"Vertices"; color: TrinityTheme.textFaint; font.pixelSize: 11; Layout.fillWidth:true} Label{ text: Trinity.viewport.meshStats.vertices || 0; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11 } }
                            RowLayout { Layout.fillWidth: true; Label{ text:"Size"; color: TrinityTheme.textFaint; font.pixelSize: 11; Layout.fillWidth:true} Label{ text: (Trinity.viewport.meshStats.sizeMm || 0) + " mm"; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11 } }
                            Button { Layout.fillWidth: true; text: "Fit View"; onClicked: Trinity.viewport.fitView() }
                        }
                    }

                    // Quick actions
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Button { Layout.fillWidth: true; text: "Run (F5)"; onClicked: Trinity.run_command_async("create a 50 mm quadcopter frame") }
                        Button { Layout.fillWidth: true; text: "Validate (F6)"; onClicked: { if (Trinity.artifactModel.count>0) Trinity.validate_artifact(Trinity.artifactModel.get(0).artifactId) } }
                    }
                }
            }

            // 1 — Engines
            ScrollView {
                clip: true
                ListView {
                    width: parent.width
                    model: Trinity.engineModel
                    spacing: 8
                    delegate: Rectangle {
                        required property var model
                        required property int index
                        width: ListView.view.width
                        implicitHeight: col.implicitHeight + 16
                        radius: TrinityTheme.radiusM
                        color: TrinityTheme.surface
                        border.color: model.health === "healthy" ? "#1E3A28" : (model.health === "scaffolded" ? "#3A2E14" : TrinityTheme.borderSoft)
                        ColumnLayout {
                            id: col
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: model.engineId; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                StatusPill { status: model.health; }
                            }
                            Label { text: model.name + " v" + model.version; color: TrinityTheme.textMuted; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Label { text: model.healthDetail.length>0 ? model.healthDetail : model.capabilities.join(" · "); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4
                                Repeater {
                                    model: model.capabilities
                                    Label {
                                        required property string modelData
                                        text: modelData
                                        color: TrinityTheme.textFaint
                                        font.family: TrinityTheme.fontMono
                                        font.pixelSize: 8
                                        padding: 4
                                        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderSoft; radius: 3 }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 2 — Selection detail + validation evidence
            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    Label { text: Trinity.viewport.selectedArtifactId.length>0 ? "Selection: " + Trinity.viewport.selectedArtifactId.substring(0,8) : "Nothing selected"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Label { visible: Trinity.viewport.selectedArtifactId.length===0; text: "Select an artifact from Artifacts or Jobs to inspect its metadata, hash and validation history."; color: TrinityTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    GroupBox {
                        visible: Trinity.viewport.selectedArtifactId.length>0
                        Layout.fillWidth: true
                        title: "Artifact"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 4
                            Label { text: "ID: " + Trinity.viewport.selectedArtifactId; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                            Label { text: "Status: " + Trinity.viewport.statusText; color: TrinityTheme.text; font.pixelSize: 11 }
                            Label { text: "Format: " + (Trinity.viewport.meshStats.format || "—") + "  ·  " + (Trinity.viewport.meshStats.triangles||0) + " tris"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }
                        }
                    }
                    Button {
                        visible: Trinity.viewport.selectedArtifactId.length>0
                        Layout.fillWidth: true
                        text: "Verify Integrity"
                        onClicked: Trinity.verify_integrity(Trinity.viewport.selectedArtifactId)
                    }
                    Button {
                        visible: Trinity.viewport.selectedArtifactId.length>0
                        Layout.fillWidth: true
                        text: "Remove"
                        onClicked: Trinity.remove_artifact(Trinity.viewport.selectedArtifactId)
                    }
                    // Validation history — real evidence rows
                    Label { visible: Trinity.viewport.selectedArtifactId.length>0; text: "VALIDATION HISTORY"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8 }
                    Repeater {
                        visible: Trinity.viewport.selectedArtifactId.length>0
                        model: Trinity.validation_history(Trinity.viewport.selectedArtifactId)
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            implicitHeight: histCol.implicitHeight + 10
                            radius: TrinityTheme.radiusM
                            color: TrinityTheme.surface
                            border.color: TrinityTheme.borderSoft
                            ColumnLayout {
                                id: histCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    StatusPill { status: modelData.status }
                                    Item { Layout.fillWidth: true }
                                    Label { text: modelData.createdAt.substring(0,19); color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 8 }
                                }
                                Label { text: modelData.engine + " · " + modelData.validationId.substring(0,8); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: modelData.checks.length>120 ? modelData.checks.substring(0,120) + "…" : modelData.checks; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.Wrap }
                            }
                        }
                    }
                    Label { visible: Trinity.viewport.selectedArtifactId.length>0 && Trinity.validation_history(Trinity.viewport.selectedArtifactId).length===0; text: "No validation rows yet — run Validate (F6)."; color: TrinityTheme.textDim; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Validation legend
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: TrinityTheme.borderSoft }
            Label { text: "VALIDATION: GENERATED > VALIDATED > VERIFIED"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 8; font.letterSpacing: 0.6; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
    }

    // Collapsed strip
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 8
        visible: collapsed
        opacity: collapsed ? 1 : 0
        ToolButton { text: "‹"; font.pixelSize: 14; onClicked: Trinity.layout.toggleRight() }
        Label { text: "INS"; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; rotation: 90 }
    }
}
