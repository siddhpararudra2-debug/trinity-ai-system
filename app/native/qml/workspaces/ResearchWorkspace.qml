import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ColumnLayout {
    spacing: 12
    anchors.fill: parent
    anchors.margins: 14

    SectionHeader { number: "08"; label: "RESEARCH — KNOWLEDGE"; detail: notesModel.count + " notes"; Layout.fillWidth: true }
    ScaffoldBanner { Layout.fillWidth: true; engineId: "research"; detail: Trinity.workspace.availabilityDetail("research"); badge: "FOUNDATION — MOCK ONLY" }

    // Search + add
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        TextField {
            id: query
            Layout.fillWidth: true
            placeholderText: "Search notes… (local, offline)"
            font.pixelSize: 12
            onTextChanged: notesProxy.filterText = text
        }
        TextField {
            id: newNoteTitle
            Layout.preferredWidth: 180
            placeholderText: "New note title…"
            font.pixelSize: 11
        }
        Button {
            text: "Add Note"
            enabled: newNoteTitle.text.trim().length>0
            onClicked: {
                notesModel.append({title: newNoteTitle.text.trim(), body: "Research note — project " + (Trinity.activeProjectId.length>0?Trinity.activeProjectId.substring(0,8):"none"), created: new Date().toISOString()})
                newNoteTitle.text=""
                persistNotes()
            }
        }
        Button { text: "Save"; onClicked: persistNotes() }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 240
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                Label { text: "DOCUMENTS — PROJECT LINKED"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8 }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: notesProxy
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        text: modelData.title
                        highlighted: ListView.isCurrentItem
                        onClicked: ListView.view.currentIndex = index
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
                RowLayout {
                    Button { text: "Remove"; enabled: notesProxy.count>0; onClicked: { notesModel.remove(notesProxy.mapToSource(notesProxy.currentIndex)); persistNotes() } }
                    Button { text: "Clear Filter"; onClicked: query.text="" }
                }
            }
        }

        Rectangle {
            SplitView.fillWidth: true
            radius: TrinityTheme.radiusM
            color: TrinityTheme.bg
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                Label { text: notesProxy.currentIndex>=0 ? notesProxy.get(notesProxy.currentIndex).title : "Select a document"; color: TrinityTheme.text; font.bold: true; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: notesProxy.currentIndex>=0 ? notesProxy.get(notesProxy.currentIndex).created : ""; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: TrinityTheme.borderSoft }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    TextArea {
                        id: noteBody
                        text: notesProxy.currentIndex>=0 ? notesProxy.get(notesProxy.currentIndex).body : "Research workspace foundation — project-linked notes, sources, citations scaffold. Future reasoning model will populate this via IEngineeringReasoner without rewriting the OS."
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                        color: TrinityTheme.textMuted
                        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                        onTextChanged: if (notesProxy.currentIndex>=0) { notesModel.setProperty(notesProxy.mapToSource(notesProxy.currentIndex), "body", text) }
                    }
                }
                RowLayout {
                    Label { text: "Sources: local files, artifact lineage, citations (future)."; color: TrinityTheme.textDim; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    Button { text: "Open Artifacts"; onClicked: Trinity.workspace.switchTo("artifacts") }
                }
            }
        }
    }

    Label { text: "Store: Settings key research.notes (JSON) — offline, per-workspace. No network, no LLM."; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }

    // Simple in-memory notes backed by Settings
    ListModel { id: notesModel }
    // Proxy for filtering
    QtObject {
        id: notesProxy
        property string filterText: ""
        property int currentIndex: 0
        property int count: filtered.length
        property var filtered: {
            if (filterText.length===0) return notesModel
            // naive filter — rebuild array of indices
            let out = []
            for (let i=0;i<notesModel.count;i++) {
                const n = notesModel.get(i)
                if (n.title.toLowerCase().indexOf(filterText.toLowerCase())>=0 || n.body.toLowerCase().indexOf(filterText.toLowerCase())>=0) out.push(n)
            }
            return out
        }
        function get(idx) {
            if (filterText.length===0) return notesModel.get(idx)
            // for filtered, naively search
            let c=0
            for (let i=0;i<notesModel.count;i++) {
                const n = notesModel.get(i)
                if (n.title.toLowerCase().indexOf(filterText.toLowerCase())>=0 || n.body.toLowerCase().indexOf(filterText.toLowerCase())>=0) {
                    if (c===idx) return n
                    c++
                }
            }
            return null
        }
        function mapToSource(proxyIdx) {
            if (filterText.length===0) return proxyIdx
            let c=0
            for (let i=0;i<notesModel.count;i++) {
                const n = notesModel.get(i)
                if (n.title.toLowerCase().indexOf(filterText.toLowerCase())>=0 || n.body.toLowerCase().indexOf(filterText.toLowerCase())>=0) {
                    if (c===proxyIdx) return i
                    c++
                }
            }
            return -1
        }
    }

    function persistNotes() {
        let arr=[]
        for(let i=0;i<notesModel.count;i++) arr.push(notesModel.get(i))
        Trinity.set_setting("research.notes", JSON.stringify(arr))
    }
    function loadNotes() {
        const raw = Trinity.get_setting("research.notes")
        if (!raw || raw.length===0) {
            // seed with one example linked to active project
            notesModel.append({title:"Welcome — Research linked to " + (Trinity.activeProjectId.length>0?Trinity.activeProjectId.substring(0,8):"workspace"), body:"Use this workspace for query/sources/documents/notes/citations. All notes are project-scoped and stored locally under Settings key research.notes.", created:new Date().toISOString()})
            return
        }
        try {
            const arr = JSON.parse(raw)
            for (let i=0;i<arr.length;i++) notesModel.append(arr[i])
        } catch(e) {}
    }
    Component.onCompleted: loadNotes()
}
