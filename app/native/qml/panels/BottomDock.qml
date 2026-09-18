// Trinity — bottom dock: Console / Jobs / Logs / Artifacts / Validation / Output
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    color: TrinityTheme.panel
    border.color: TrinityTheme.border
    property bool collapsed: Trinity.layout.bottomCollapsed
    property bool undocked: Trinity.layout.bottomUndocked
    property int currentTab: Trinity.layout.bottomTab
    onCurrentTabChanged: Trinity.layout.bottomTab = currentTab

    // Header
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        opacity: collapsed ? 0 : 1
        visible: !collapsed

        // Tab bar + controls
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            spacing: 6
            Layout.leftMargin: 8
            Layout.rightMargin: 8

            TabBar {
                id: bar
                currentIndex: currentTab
                onCurrentIndexChanged: currentTab = currentIndex
                background: Rectangle { color: "transparent" }
                TabButton { text: "Console"; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 90 }
                TabButton { text: "Jobs  • " + Trinity.jobModel.count; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 110 }
                TabButton { text: "Logs"; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 80 }
                TabButton { text: "Artifacts • " + Trinity.artifactModel.count; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 130 }
                TabButton { text: "Validation"; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 110 }
                TabButton { text: "Output"; font.family: TrinityTheme.fontMono; font.pixelSize: 10; width: 90 }
            }
            Item { Layout.fillWidth: true }
            Label { text: Trinity.jobModel.activeCount + " active · " + Trinity.jobModel.queuedCount + " queued"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; visible: Trinity.jobModel.activeCount>0 || Trinity.jobModel.queuedCount>0 }
            ToolButton { text: "◫"; font.pixelSize: 11; onClicked: Trinity.layout.bottomUndocked = !Trinity.layout.bottomUndocked; ToolTip.text: Trinity.layout.bottomUndocked ? "Dock" : "Undock"; ToolTip.visible: hovered }
            ToolButton { text: "⌄"; font.pixelSize: 12; onClicked: Trinity.layout.toggleBottom(); ToolTip.text: "Collapse"; ToolTip.visible: hovered }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: TrinityTheme.borderSoft }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentTab

            // 0 — Console
            ColumnLayout {
                Layout.margins: 10
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: "❯"; color: TrinityTheme.accentBright; font.bold: true; font.pixelSize: 14 }
                    TextField {
                        id: consoleInput
                        Layout.fillWidth: true
                        placeholderText: "create a 50 mm quadcopter frame  ·  calculate 2*pi*25  ·  help"
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 12
                        color: TrinityTheme.text
                        background: Rectangle { color: TrinityTheme.bg; border.color: TrinityTheme.border; radius: TrinityTheme.radiusM }
                        onAccepted: {
                            if (text.trim().length===0) return;
                            const res = Trinity.run_command_async(text.trim());
                            consoleOutput.text = res.ok ? ("→ job " + res.job_id + "  queued") : ("✗ " + res.error);
                            // keep history
                            historyModel.insert(0, { text: text.trim() });
                            text = "";
                        }
                    }
                    Button {
                        text: "Run (F5)"
                        onClicked: consoleInput.accepted()
                        highlighted: true
                    }
                    Button {
                        text: "Clear"
                        onClicked: consoleOutput.text = ""
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: consoleOutput
                        readOnly: true
                        wrapMode: Text.Wrap
                        color: TrinityTheme.textMuted
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 11
                        placeholderText: "Output appears here · job progress streams to Jobs/Logs"
                        background: Rectangle { color: TrinityTheme.bg; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    orientation: ListView.Horizontal
                    spacing: 6
                    model: ListModel {
                        id: historyModel
                        ListElement { text: "create a 50 mm quadcopter frame" }
                        ListElement { text: "calculate 20% of 50" }
                        ListElement { text: "help" }
                    }
                    delegate: Button {
                        required property var model
                        text: model.text
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 10
                        onClicked: consoleInput.text = model.text
                    }
                }
            }

            // 1 — Jobs
            ColumnLayout {
                Layout.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.jobModel.count + " jobs"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true }
                    Button { text: "Refresh"; onClicked: Trinity.jobModel.refresh(100) }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Trinity.jobModel
                    spacing: 4
                    delegate: Rectangle {
                        required property var model
                        required property int index
                        width: ListView.view.width
                        height: 44
                        radius: TrinityTheme.radiusM
                        color: model.status === "FAILED" ? TrinityTheme.dangerBg : (model.status === "COMPLETED" ? TrinityTheme.successBg : TrinityTheme.surface)
                        border.color: model.status === "FAILED" ? "#3A1E1A" : (model.status === "COMPLETED" ? "#1E2A22" : TrinityTheme.borderSoft)
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: model.jobId.substring(0,8) + "  " + model.engine + "." + model.operation; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: model.status + " · " + Math.round(model.progress*100) + "%  ·  " + model.createdAt; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            StatusPill { status: model.status }
                            ProgressBar { from: 0; to: 1; value: model.progress; Layout.preferredWidth: 80; visible: model.status==="RUNNING" || model.status==="QUEUED" }
                            RowLayout {
                                spacing: 4
                                Button { text: "Pause"; visible: model.status==="RUNNING"; onClicked: Trinity.jobModel.pauseJob(model.jobId); font.pixelSize: 10 }
                                Button { text: "Resume"; visible: model.status==="PAUSED"; onClicked: Trinity.jobModel.resumeJob(model.jobId); font.pixelSize: 10 }
                                Button { text: "Cancel"; visible: model.status==="RUNNING"||model.status==="QUEUED"||model.status==="PAUSED"; onClicked: Trinity.jobModel.cancelJob(model.jobId); font.pixelSize: 10 }
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }

            // 2 — Logs
            ColumnLayout {
                Layout.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: logFilter; Layout.fillWidth: true; placeholderText: "Filter logs…"; font.family: TrinityTheme.fontMono; font.pixelSize: 11; onTextChanged: Trinity.logModel.filterText = text }
                    Button { text: "Clear"; onClicked: Trinity.logModel.clear() }
                    Button { text: "Refresh"; onClicked: Trinity.logModel.refresh(500) }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Trinity.logModel
                    delegate: Label {
                        required property var model
                        width: ListView.view.width
                        text: "[" + model.timestamp + "] " + model.component + "  " + model.message
                        color: model.level==="Error" ? "#E88A7A" : (model.level==="Warning" ? "#C9B07A" : TrinityTheme.textMuted)
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        padding: 4
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }

            // 3 — Artifacts
            ColumnLayout {
                Layout.margins: 8
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: Trinity.artifactModel.count + " artifacts" + (Trinity.activeProjectId.length>0 ? " · project " + Trinity.activeProjectId.substring(0,8) : " · no project"); color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Button { text: "Refresh"; onClicked: Trinity.artifactModel.refresh() }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: Trinity.artifactModel
                    delegate: Rectangle {
                        required property var model
                        width: ListView.view.width
                        height: 48
                        radius: TrinityTheme.radiusM
                        color: TrinityTheme.surface
                        border.color: TrinityTheme.borderSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label { text: model.type.toUpperCase() + "  " + model.filename; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                Label { text: model.validationState + " · " + model.hash.substring(0,12) + " · " + model.sizeBytes + " B"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            StatusPill { status: model.validationState }
                            Button { text: "Select"; onClicked: { Trinity.viewport.selectedArtifactId = model.artifactId; Trinity.viewport.refreshStats(model.artifactId) } }
                            Button { text: "Validate"; onClicked: Trinity.validate_artifact(model.artifactId) }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }

            // 4 — Validation — evidence table
            ColumnLayout {
                Layout.margins: 10
                spacing: 8
                Label { text: "VALIDATION LIFECYCLE — GENERATED → VALIDATED → VERIFIED"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.6; Layout.fillWidth: true; wrapMode: Text.Wrap }
                Label {
                    Layout.fillWidth: true
                    text: Trinity.viewport.selectedArtifactId.length>0 ? ("Artifact " + Trinity.viewport.selectedArtifactId.substring(0,12) + " · " + Trinity.viewport.statusText) : "Select an artifact to view validation history."
                    color: TrinityTheme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button { text: "Validate"; enabled: Trinity.viewport.selectedArtifactId.length>0; onClicked: Trinity.validate_artifact(Trinity.viewport.selectedArtifactId) }
                    Button { text: "Verify"; enabled: Trinity.viewport.selectedArtifactId.length>0; onClicked: Trinity.verify_artifact(Trinity.viewport.selectedArtifactId) }
                    Button { text: "Check Integrity"; enabled: Trinity.viewport.selectedArtifactId.length>0; onClicked: Trinity.verify_integrity(Trinity.viewport.selectedArtifactId) }
                    Button { text: "Refresh"; enabled: Trinity.viewport.selectedArtifactId.length>0; onClicked: validationRepeater.model = Trinity.validation_history(Trinity.viewport.selectedArtifactId) }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: TrinityTheme.borderSoft }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 4
                        Label { text: "Evidence — per-check engine verification (never VERIFIED without checks)."; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Repeater {
                            id: validationRepeater
                            model: Trinity.viewport.selectedArtifactId.length>0 ? Trinity.validation_history(Trinity.viewport.selectedArtifactId) : []
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                implicitHeight: vcol.implicitHeight + 10
                                radius: TrinityTheme.radiusM
                                color: TrinityTheme.surface
                                border.color: TrinityTheme.borderSoft
                                ColumnLayout {
                                    id: vcol
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 2
                                    RowLayout {
                                        Layout.fillWidth: true
                                        StatusPill { status: modelData.status }
                                        Item { Layout.fillWidth: true }
                                        Label { text: modelData.createdAt.substring(0,19); color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 8 }
                                    }
                                    Label { text: modelData.engine + " · " + modelData.validationId.substring(0,8); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }
                                    Label { text: modelData.checks.length>160 ? modelData.checks.substring(0,160)+"…" : modelData.checks; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 8; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                }
                            }
                        }
                        Label { visible: Trinity.viewport.selectedArtifactId.length>0 && validationRepeater.count===0; text: "No rows yet — run Validate (F6) to produce GENER... → VALIDATED evidence."; color: TrinityTheme.textDim; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                }
            }

            // 5 — Output
            ScrollView {
                clip: true
                TextArea {
                    readOnly: true
                    wrapMode: Text.Wrap
                    color: TrinityTheme.textMuted
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 11
                    text: {
                        const j = Trinity.jobModel.count>0 ? Trinity.jobModel.get(0) : null;
                        if (!j) return "No jobs yet — run a command from Console or CAD.";
                        return JSON.stringify(j, null, 2);
                    }
                    background: Rectangle { color: TrinityTheme.bg; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                }
            }
        }
    }

    // Collapsed bar
    RowLayout {
        anchors.centerIn: parent
        spacing: 8
        visible: collapsed
        Label { text: "CONSOLE / JOBS / LOGS / ARTIFACTS"; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8 }
        ToolButton { text: "⌃"; font.pixelSize: 12; onClicked: Trinity.layout.toggleBottom() }
    }
}
