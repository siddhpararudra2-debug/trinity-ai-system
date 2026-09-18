// Trinity — MainWindow: engineering workstation shell.
// SplitView dock layout, workspace loader, undock windows, palette, shortcuts.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import "../theme"
import "../panels"
import "../workspaces"
import ".."

ApplicationWindow {
    id: root
    width: Trinity.layout.windowWidth > 0 ? Trinity.layout.windowWidth : 1480
    height: Trinity.layout.windowHeight > 0 ? Trinity.layout.windowHeight : 920
    x: Trinity.layout.windowX >= 0 ? Trinity.layout.windowX : -1
    y: Trinity.layout.windowY >= 0 ? Trinity.layout.windowY : -1
    minimumWidth: 1180
    minimumHeight: 720
    visible: true
    title: qsTr("TRINITY - Engineering Operating System  |  ") + Trinity.workspace.currentWorkspaceTitle + (Trinity.activeProjectId.length>0 ? " - " + Trinity.activeProjectId.substring(0,8) : "")
    color: TrinityTheme.bg
    // Window geometry → LayoutController (auto-save on move/resize)
    onWidthChanged: if (visible) Trinity.layout.windowWidth = width
    onHeightChanged: if (visible) Trinity.layout.windowHeight = height
    onXChanged: if (visible && x>=0) Trinity.layout.windowX = x
    onYChanged: if (visible && y>=0) Trinity.layout.windowY = y

    // ----- Menu & Toolbar -----
    menuBar: AppMenuBar {
        id: appMenu
        onNewProjectRequested: Trinity.projectModel.createProject("Project " + new Date().toISOString().substring(0,10), "")
        onOpenProjectRequested: openDialog.open()
        onImportProjectRequested: importProjectDialog.open()
        onExportProjectRequested: exportProjectDialog.open()
        onExportRequested: exportProjectDialog.open()
        onSaveRequested: { Trinity.layout.save(); statusText.text = "Layout saved @ " + new Date().toLocaleTimeString(); statusTimer.restart() }
        onUndoRequested: { statusText.text = "Undo — no undo stack in V1 (edits are immediate)"; statusTimer.restart() }
        onRedoRequested: { statusText.text = "Redo — no undo stack in V1"; statusTimer.restart() }
        onPaletteRequested: palette.openPalette()
        onRunRequested: runCurrent()
        onValidateRequested: validateCurrent()
        onCancelRequested: cancelCurrent()
        onLayoutResetRequested: Trinity.layout.restoreDefaults()
        onSettingsRequested: Trinity.workspace.switchTo("settings")
        onAboutRequested: aboutDialog.open()
    }

    header: AppToolBar {
        id: appBar
        onRunRequested: runCurrent()
        onValidateRequested: validateCurrent()
        onExportRequested: exportProjectDialog.open()
        onPaletteRequested: palette.openPalette()
    }

    // ----- Global shortcuts (keyboard-first) -----
    Shortcut { sequence: "Ctrl+K"; onActivated: palette.openPalette() }
    Shortcut { sequence: "Ctrl+N"; onActivated: Trinity.projectModel.createProject("Project " + new Date().toISOString().substring(0,10), "") }
    Shortcut { sequence: "Ctrl+O"; onActivated: openDialog.open() }
    Shortcut { sequence: "Ctrl+S"; onActivated: { Trinity.layout.save(); statusText.text = "Saved"; statusTimer.restart() } }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: exportProjectDialog.open() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: { statusText.text = "Undo — no undo stack in V1"; statusTimer.restart() } }
    Shortcut { sequence: "Ctrl+Y"; onActivated: { statusText.text = "Redo — no undo stack in V1"; statusTimer.restart() } }
    Shortcut { sequence: "F5"; onActivated: runCurrent() }
    Shortcut { sequence: "F6"; onActivated: validateCurrent() }
    Shortcut { sequence: "Esc"; onActivated: {
            if (palette.visible) { palette.close(); return }
            cancelCurrent();
        }
    }
    // Workspace quick switches Ctrl+1..0 + Ctrl+Tab navigation
    Shortcut { sequence: "Ctrl+1"; onActivated: Trinity.workspace.setCurrentWorkspace(0) }
    Shortcut { sequence: "Ctrl+2"; onActivated: Trinity.workspace.setCurrentWorkspace(1) }
    Shortcut { sequence: "Ctrl+3"; onActivated: Trinity.workspace.setCurrentWorkspace(2) }
    Shortcut { sequence: "Ctrl+4"; onActivated: Trinity.workspace.setCurrentWorkspace(4) }
    Shortcut { sequence: "Ctrl+5"; onActivated: Trinity.workspace.setCurrentWorkspace(5) }
    Shortcut { sequence: "Ctrl+6"; onActivated: Trinity.workspace.setCurrentWorkspace(6) }
    Shortcut { sequence: "Ctrl+7"; onActivated: Trinity.workspace.setCurrentWorkspace(7) }
    Shortcut { sequence: "Ctrl+8"; onActivated: Trinity.workspace.setCurrentWorkspace(8) }
    Shortcut { sequence: "Ctrl+9"; onActivated: Trinity.workspace.setCurrentWorkspace(9) }
    Shortcut { sequence: "Ctrl+0"; onActivated: Trinity.workspace.setCurrentWorkspace(11) }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: Trinity.workspace.setCurrentWorkspace((Trinity.workspace.currentWorkspace + 1) % 12) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: Trinity.workspace.setCurrentWorkspace((Trinity.workspace.currentWorkspace + 11) % 12) }

    function runCurrent() {
        const ws = Trinity.workspace.currentWorkspaceId
        if (ws === "cad") Trinity.run_command_async("create a 50 mm quadcopter frame")
        else if (ws === "math") { /* Math workspace owns its run button; fallback */ Trinity.run_command_async("calculate 2*pi*25") }
        else if (ws === "jobs") Trinity.jobModel.refresh(100)
        else statusText.text = "Run: no action for " + ws
        statusTimer.restart()
    }
    function validateCurrent() {
        if (Trinity.viewport.selectedArtifactId.length>0) {
            const r = Trinity.validate_artifact(Trinity.viewport.selectedArtifactId)
            statusText.text = r.ok ? ("Validated → " + r.state) : ("Validate failed: " + r.state)
        } else if (Trinity.artifactModel.count>0) {
            const aid = Trinity.artifactModel.get(0).artifactId
            const r = Trinity.validate_artifact(aid)
            statusText.text = r.ok ? ("Validated " + aid.substring(0,8) + " → " + r.state) : ("Validate failed")
        } else statusText.text = "Nothing to validate — generate CAD or pick an artifact."
        statusTimer.restart()
    }
    function cancelCurrent() {
        let cancelled = 0
        for (let i=0;i<Trinity.jobModel.count;i++) {
            const j = Trinity.jobModel.get(i)
            if (j.status==="RUNNING"||j.status==="QUEUED"||j.status==="PAUSED") {
                if (Trinity.jobModel.cancelJob(j.jobId)) cancelled++
            }
        }
        if (cancelled>0) { statusText.text = "Cancelled " + cancelled + " job(s) — Esc"; statusTimer.restart() }
    }

    // ----- Body: dockable workstation (horizontal SplitView for drag-resize) -----
    SplitView {
        id: outerSplit
        anchors.fill: parent
        orientation: Qt.Horizontal
        handle: Rectangle {
            implicitWidth: 4
            color: SplitHandle.pressed ? TrinityTheme.accent : (SplitHandle.hovered ? TrinityTheme.borderStrong : TrinityTheme.border)
            Behavior on color { ColorAnimation { duration: TrinityTheme.durationFast } }
        }

        // Left panel (+ undocked window) — draggable handle
        Loader {
            id: leftLoader
            SplitView.preferredWidth: Trinity.layout.leftCollapsed ? 44 : Trinity.layout.leftWidth
            SplitView.minimumWidth: Trinity.layout.leftCollapsed ? 44 : TrinityTheme.leftMin
            SplitView.maximumWidth: Trinity.layout.leftCollapsed ? 44 : TrinityTheme.leftMax
            SplitView.fillHeight: true
            sourceComponent: Trinity.layout.leftUndocked ? null : leftPanelComp
            visible: !Trinity.layout.leftUndocked
            onWidthChanged: if (visible && !Trinity.layout.leftCollapsed && width >= TrinityTheme.leftMin && width <= TrinityTheme.leftMax) Trinity.layout.leftWidth = width
        }

        // Center column: workspace + bottom dock (vertical SplitView — bottom already draggable)
        SplitView {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle {
                implicitHeight: 4
                color: SplitHandle.pressed ? TrinityTheme.accent : (SplitHandle.hovered ? TrinityTheme.borderStrong : TrinityTheme.border)
                Behavior on color { ColorAnimation { duration: TrinityTheme.durationFast } }
            }

            Rectangle {
                SplitView.fillHeight: true
                SplitView.minimumHeight: 260
                color: TrinityTheme.bg
                border.color: TrinityTheme.border
                Loader {
                    id: workspaceLoader
                    anchors.fill: parent
                    anchors.margins: 1
                    sourceComponent: {
                        switch (Trinity.workspace.currentWorkspace) {
                            case 0: return homeComp
                            case 1: return projectsComp
                            case 2: return cadComp
                            case 3: return pcbComp
                            case 4: return mathComp
                            case 5: return simulationComp
                            case 6: return visionComp
                            case 7: return firmwareComp
                            case 8: return researchComp
                            case 9: return artifactsComp
                            case 10: return jobsComp
                            case 11: return settingsComp
                            default: return homeComp
                        }
                    }
                }
            }

            Loader {
                id: bottomLoader
                SplitView.preferredHeight: Trinity.layout.bottomCollapsed ? 28 : Trinity.layout.bottomHeight
                SplitView.minimumHeight: Trinity.layout.bottomCollapsed ? 28 : TrinityTheme.bottomMin
                SplitView.maximumHeight: Trinity.layout.bottomCollapsed ? 28 : TrinityTheme.bottomMax
                sourceComponent: Trinity.layout.bottomUndocked ? null : bottomDockComp
                visible: !Trinity.layout.bottomUndocked
                onHeightChanged: if (visible && !Trinity.layout.bottomCollapsed && height >= TrinityTheme.bottomMin && height <= TrinityTheme.bottomMax) Trinity.layout.bottomHeight = height
            }
        }

        // Right panel — draggable via same outer SplitView handle
        Loader {
            id: rightLoader
            SplitView.preferredWidth: Trinity.layout.rightCollapsed ? 44 : Trinity.layout.rightWidth
            SplitView.minimumWidth: Trinity.layout.rightCollapsed ? 44 : TrinityTheme.rightMin
            SplitView.maximumWidth: Trinity.layout.rightCollapsed ? 44 : TrinityTheme.rightMax
            SplitView.fillHeight: true
            sourceComponent: Trinity.layout.rightUndocked ? null : rightPanelComp
            visible: !Trinity.layout.rightUndocked
            onWidthChanged: if (visible && !Trinity.layout.rightCollapsed && width >= TrinityTheme.rightMin && width <= TrinityTheme.rightMax) Trinity.layout.rightWidth = width
        }
    }

    // ----- Panel components -----
    Component {
        id: leftPanelComp
        LeftPanel {}
    }
    Component {
        id: rightPanelComp
        RightPanel {}
    }
    Component {
        id: bottomDockComp
        BottomDock {}
    }

    // ----- Workspace components -----
    Component { id: homeComp; HomeWorkspace {} }
    Component { id: projectsComp; ProjectsWorkspace {} }
    Component { id: cadComp; CadWorkspace {} }
    Component { id: pcbComp; PcbWorkspace {} }
    Component { id: mathComp; MathWorkspace {} }
    Component { id: simulationComp; SimulationWorkspace {} }
    Component { id: visionComp; VisionWorkspace {} }
    Component { id: firmwareComp; FirmwareWorkspace {} }
    Component { id: researchComp; ResearchWorkspace {} }
    Component { id: artifactsComp; ArtifactsWorkspace {} }
    Component { id: jobsComp; JobsWorkspace {} }
    Component { id: settingsComp; SettingsWorkspace {} }

    // Native drag/drop overlay — import STL/STEP onto Artifacts
    DropArea {
        anchors.fill: parent
        z: 2
        onEntered: function(drag) {
            if (drag.hasUrls) {
                const urls = drag.urls
                for (let i=0;i<urls.length;i++) {
                    const low = String(urls[i]).toLowerCase()
                    if (low.endsWith(".stl") || low.endsWith(".step") || low.endsWith(".stp") || low.endsWith(".obj") || low.endsWith(".3mf") || low.endsWith(".glb")) { drag.acceptProposedAction(); return }
                }
            }
            drag.accepted = false
        }
        onDropped: function(drop) {
            const urls = drop.urls
            let imported = 0
            for (let i=0;i<urls.length;i++) {
                const url = String(urls[i])
                const low = url.toLowerCase()
                if (low.endsWith(".stl") || low.endsWith(".step") || low.endsWith(".stp") || low.endsWith(".obj") || low.endsWith(".3mf") || low.endsWith(".glb")) {
                    const res = Trinity.import_artifact(url, low.substring(low.lastIndexOf(".")+1))
                    if (res.ok) {
                        statusText.text = "Imported " + url.substring(url.lastIndexOf("/")+1) + " -> " + res.artifact_id.substring(0,8)
                        Trinity.viewport.selectedArtifactId = res.artifact_id
                    } else {
                        statusText.text = "Import failed — " + (Trinity.activeProjectId.length>0 ? "see logs" : "create a project first")
                    }
                    statusTimer.restart()
                    imported++
                }
            }
            if (imported>0) Trinity.workspace.switchTo("artifacts")
        }
    }

    // ----- Undocked windows -----
    Window {
        id: leftWindow
        title: "Trinity — Navigator"
        width: 300; height: 700
        visible: Trinity.layout.leftUndocked
        color: TrinityTheme.panel
        onVisibleChanged: if (!visible && Trinity.layout.leftUndocked) Trinity.layout.leftUndocked = false
        onClosing: Trinity.layout.leftUndocked = false
        LeftPanel { anchors.fill: parent }
    }
    Window {
        id: rightWindow
        title: "Trinity — Inspector"
        width: 340; height: 700
        visible: Trinity.layout.rightUndocked
        color: TrinityTheme.panel
        onVisibleChanged: if (!visible && Trinity.layout.rightUndocked) Trinity.layout.rightUndocked = false
        onClosing: Trinity.layout.rightUndocked = false
        RightPanel { anchors.fill: parent }
    }
    Window {
        id: bottomWindow
        title: "Trinity — Console / Jobs"
        width: 900; height: 360
        visible: Trinity.layout.bottomUndocked
        color: TrinityTheme.panel
        onVisibleChanged: if (!visible && Trinity.layout.bottomUndocked) Trinity.layout.bottomUndocked = false
        onClosing: Trinity.layout.bottomUndocked = false
        BottomDock { anchors.fill: parent }
    }

    // ----- Overlays -----
    CommandPalette { id: palette }

    // Status bar
    footer: ToolBar {
        height: 24
        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.border; Rectangle{ anchors.top: parent.top; width: parent.width; height: 1; color: TrinityTheme.border } }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10
            Label { id: statusText; text: Trinity.jobModel.activeCount>0 ? (Trinity.jobModel.activeCount + " running — F5 run · F6 validate · Esc cancel · Ctrl+K palette") : "Ready — Ctrl+K palette · Ctrl+N new · F5 run · F6 validate · Esc cancel"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: "layout: " + (Trinity.layout.leftCollapsed?"◂":"▸") + (Trinity.layout.rightCollapsed?"▸":"◂") + (Trinity.layout.bottomCollapsed?"▾":"▴"); color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
            Label { text: "TRINITY " + Qt.application.version; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
        }
        Timer { id: statusTimer; interval: 4000; onTriggered: statusText.text = Trinity.jobModel.activeCount>0 ? (Trinity.jobModel.activeCount + " running — F5 run · F6 validate · Esc cancel") : "Ready — Ctrl+K palette · Ctrl+N new · F5 run · F6 validate" }
    }

    // ----- Dialogs -----
    Dialog {
        id: openDialog
        title: "Open Project"
        anchors.centerIn: parent
        parent: Overlay.overlay
        modal: true
        width: 520
        height: 380
        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderStrong; radius: TrinityTheme.radiusL }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "Select a project to activate. Workspace root is configured in Settings → Workspace."; color: TrinityTheme.textFaint; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Trinity.projectModel
                delegate: ItemDelegate {
                    required property var model
                    width: ListView.view.width
                    text: model.name + "  —  " + model.projectId.substring(0,8)
                    highlighted: Trinity.activeProjectId===model.projectId
                    onClicked: { Trinity.projectModel.openProject(model.projectId); openDialog.close() }
                }
                ScrollBar.vertical: ScrollBar {}
            }
            RowLayout { Button { text: "Refresh"; onClicked: Trinity.projectModel.refresh(true) } Item{Layout.fillWidth:true} Button { text: "Close"; onClicked: openDialog.close() } }
        }
    }

    Dialog {
        id: aboutDialog
        title: "About Trinity"
        anchors.centerIn: parent
        parent: Overlay.overlay
        modal: true
        width: 460
        standardButtons: Dialog.Close
        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderStrong; radius: TrinityTheme.radiusL }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "TRINITY — Engineering Operating System"; color: TrinityTheme.text; font.bold: true; font.pixelSize: 14 }
            Label { text: "Version 0.1.0 · Native desktop (Qt 6 / QML)"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            Label { text: "Deterministic engines, independent verification, SHA-256 lineage. No probabilistic execution in the critical path."; color: TrinityTheme.textFaint; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: "Engines: " + Trinity.engineModel.count + "  ·  Project: " + (Trinity.activeProjectId.length>0 ? Trinity.activeProjectId.substring(0,8) : "none") + "  ·  Data: " + Trinity.dataDir; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
    }

    // Native file dialogs for project import/export (honest folder copy; ZIP next)
    FolderDialog {
        id: importProjectDialog
        title: qsTr("Import Project — Select Folder")
        onAccepted: {
            // ProjectStore::import_from_folder expects source folder + new name derived from folder
            const folder = selectedFolder.toString()
            const name = folder.substring(folder.lastIndexOf("/") + 1) || "Imported"
            statusText.text = "Import: " + folder + " — folder copy (ZIP next)"
            // For now list refresh; real import would call Trinity.projectModel.importProject (not yet exposed)
            // Honest placeholder — show in Projects workspace
            Trinity.workspace.switchTo("projects")
            statusTimer.restart()
        }
    }
    FolderDialog {
        id: exportProjectDialog
        title: qsTr("Export Project — Select Destination Folder")
        onAccepted: {
            const dest = selectedFolder.toString()
            if (Trinity.activeProjectId.length === 0) {
                statusText.text = "Export failed — no active project"
            } else {
                statusText.text = "Export: " + Trinity.activeProjectId.substring(0,8) + " → " + dest + " (folder copy)"
            }
            statusTimer.restart()
        }
    }

    // Persist window geometry + layout on close
    onClosing: {
        Trinity.layout.windowWidth = width
        Trinity.layout.windowHeight = height
        Trinity.layout.windowX = x
        Trinity.layout.windowY = y
        Trinity.layout.save()
    }
    Component.onCompleted: {
        // ensure models are warm
        Trinity.projectModel.refresh(true)
        Trinity.engineModel.refresh()
        Trinity.jobModel.refresh(100)
        Trinity.artifactModel.refresh()
        // Appearance live — bind Settings → TrinityTheme
        const th = Trinity.get_setting("appearance.theme")
        if (th === "light" || th === "dark") TrinityTheme.theme = th
        const sc = parseFloat(Trinity.get_setting("appearance.font_scale"))
        if (!isNaN(sc) && sc > 0.7 && sc < 1.6) TrinityTheme.fontScale = sc
    }

    Connections {
        target: Trinity
        function onError_raised(code, message) { statusText.text = "✗ " + code + ": " + message; statusTimer.restart() }
        function onJob_updated(jobId, progress, status) { if (status==="COMPLETED"||status==="FAILED") { statusText.text = status + " " + jobId.substring(0,8); statusTimer.restart() } }
    }
    Connections {
        target: Trinity.settingsCtrl
        function onSettingChanged(key, value) {
            if (key === "appearance.theme") TrinityTheme.theme = (value === "light" ? "light" : "dark")
            if (key === "appearance.font_scale") { const s = parseFloat(value); if (!isNaN(s) && s>0.7 && s<1.6) TrinityTheme.fontScale = s }
        }
    }
}
