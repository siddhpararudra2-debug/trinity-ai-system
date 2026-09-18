// Trinity — ARTIFACTS workspace: lineage grid
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ColumnLayout {
    spacing: 12
    anchors.fill: parent
    anchors.margins: 14

    SectionHeader { number: "09"; label: "ARTIFACTS — LINEAGE"; detail: Trinity.artifactModel.count + " objects"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: Trinity.activeProjectId.length>0 ? "Project " + Trinity.activeProjectId.substring(0,8) : "No project selected — artifacts are project-scoped."; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle }
        Button { text: "Refresh"; onClicked: Trinity.artifactModel.refresh() }
        Button { text: "Open Folder"; enabled: Trinity.activeProjectId.length>0; onClicked: Qt.openUrlExternally("file:///" + Trinity.dataDir) }
    }

    GridView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        cellWidth: 300
        cellHeight: 132
        model: Trinity.artifactModel
        delegate: Rectangle {
            required property var model
            width: 290
            height: 122
            radius: TrinityTheme.radiusL
            color: TrinityTheme.surface
            border.color: Trinity.viewport.selectedArtifactId===model.artifactId ? TrinityTheme.accentBright : TrinityTheme.border
            border.width: Trinity.viewport.selectedArtifactId===model.artifactId ? 1.5 : 1
            MouseArea {
                anchors.fill: parent
                onClicked: { Trinity.viewport.selectedArtifactId = model.artifactId; Trinity.viewport.refreshStats(model.artifactId); }
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: model.type.toUpperCase(); color: TrinityTheme.accentBright; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.bold: true }
                    Item { Layout.fillWidth: true }
                    StatusPill { status: model.validationState }
                }
                Label { text: model.filename; color: TrinityTheme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideMiddle }
                Label { text: model.hash.substring(0,16) + "…  ·  " + model.sizeBytes + " B"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: model.artifactId.substring(0,8) + " · " + model.createdAt; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 8; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                RowLayout {
                    spacing: 4
                    Button { text: "Select"; onClicked: { Trinity.viewport.selectedArtifactId = model.artifactId; Trinity.viewport.refreshStats(model.artifactId) }  }
                    Button { text: "Validate"; onClicked: Trinity.validate_artifact(model.artifactId) }
                    Button { text: "Verify"; onClicked: Trinity.verify_artifact(model.artifactId) }
                    Button { text: "Delete"; onClicked: Trinity.remove_artifact(model.artifactId) }
                }
            }
        }
        ScrollBar.vertical: ScrollBar {}
    }

    Label { Layout.fillWidth: true; text: "Artifacts are content-addressed (SHA-256) and single-writer. Validation: GENERATED → VALIDATED → VERIFIED. Tampered files fail verify_integrity."; color: TrinityTheme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap }
}
