// Trinity — PROJECTS workspace: full project lifecycle
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"

ColumnLayout {
    spacing: 12
    anchors.fill: parent
    anchors.margins: 14

    SectionHeader { number: "01"; label: "PROJECTS"; detail: Trinity.projectModel.count + " total"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Search projects…"
            font.family: TrinityTheme.fontMono
            onTextChanged: {} // filtering handled client-side if needed
        }
        Button { text: "New Project… (Ctrl+N)"; highlighted: true; onClicked: newDialog.open() }
        Button { text: "Refresh"; onClicked: Trinity.projectModel.refresh(true) }
        CheckBox { id: showArchived; text: "Archived"; checked: false; onToggled: Trinity.projectModel.refresh(checked) }
    }

    // Table
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: TrinityTheme.radiusM
        color: TrinityTheme.panel
        border.color: TrinityTheme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 0

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Label { text: "NAME"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.preferredWidth: 220 }
                Label { text: "ID"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.preferredWidth: 120 }
                Label { text: "WORKSPACE"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.fillWidth: true }
                Label { text: "ACTIONS"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.preferredWidth: 180 }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft; Layout.topMargin: 6; Layout.bottomMargin: 6 }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Trinity.projectModel
                spacing: 4
                delegate: Rectangle {
                    required property var model
                    required property int index
                    width: ListView.view.width
                    height: 48
                    radius: TrinityTheme.radiusM
                    color: Trinity.activeProjectId === model.projectId ? TrinityTheme.surface : "transparent"
                    border.color: Trinity.activeProjectId === model.projectId ? TrinityTheme.borderStrong : TrinityTheme.borderSoft
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10
                        ColumnLayout {
                            Layout.preferredWidth: 220
                            spacing: 2
                            Label { text: model.name; color: TrinityTheme.text; font.pixelSize: 12; font.bold: Trinity.activeProjectId===model.projectId; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: model.description.length>0 ? model.description : "—"; color: TrinityTheme.textFaint; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        Label { text: model.projectId.substring(0,12); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.preferredWidth: 120; elide: Text.ElideMiddle }
                        Label { text: model.workspace; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        RowLayout {
                            Layout.preferredWidth: 180
                            spacing: 4
                            Button {
                                text: Trinity.activeProjectId===model.projectId ? "Active" : "Open"
                                enabled: Trinity.activeProjectId!==model.projectId
                                onClicked: Trinity.projectModel.openProject(model.projectId)
                                font.pixelSize: 10
                            }
                            Button {
                                text: model.archived ? "Unarchive" : "Archive"
                                onClicked: Trinity.projectModel.archiveProject(model.projectId, !model.archived)
                                font.pixelSize: 10
                            }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }

    Label { text: "Tip: Ctrl+O opens a folder · Export is scaffolded (folder copy only) — ZIP packaging ships next."; color: TrinityTheme.textDim; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.Wrap }

    Dialog {
        id: newDialog
        title: "New Project"
        anchors.centerIn: Overlay.overlay
        parent: Overlay.overlay
        modal: true
        width: 460
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; radius: TrinityTheme.radiusL }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "Project name is required. Workspace is created under %LOCALAPPDATA%/Trinity or configured root."; color: TrinityTheme.textFaint; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            TextField { id: pName; placeholderText: "e.g. Quadcopter v2"; Layout.fillWidth: true }
            TextField { id: pDesc; placeholderText: "Description (optional)"; Layout.fillWidth: true }
        }
        onAccepted: {
            if (pName.text.trim().length===0) return;
            Trinity.projectModel.createProject(pName.text.trim(), pDesc.text.trim());
            pName.text=""; pDesc.text="";
        }
    }
}
