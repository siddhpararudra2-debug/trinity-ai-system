// Trinity — JOBS workspace: full job monitor
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

ColumnLayout {
    spacing: 10
    anchors.fill: parent
    anchors.margins: 14

    SectionHeader { number: "10"; label: "JOBS — EXECUTION MONITOR"; detail: Trinity.jobModel.count + " total · " + Trinity.jobModel.activeCount + " active"; Layout.fillWidth: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Button { text: "Refresh"; onClicked: Trinity.jobModel.refresh(100) }
        Button { text: "Cancel All Active"; enabled: Trinity.jobModel.activeCount>0; onClicked: {
                for (let i=0;i<Trinity.jobModel.count;i++){ const j=Trinity.jobModel.get(i); if (j.status==="RUNNING"||j.status==="QUEUED") Trinity.jobModel.cancelJob(j.jobId) }
            }
        }
        Item { Layout.fillWidth: true }
        Label { text: "Queue: " + Trinity.jobModel.queuedCount + "  Active: " + Trinity.jobModel.activeCount; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Horizontal

        // List
        Rectangle {
            SplitView.preferredWidth: 520
            SplitView.minimumWidth: 320
            radius: TrinityTheme.radiusM
            color: TrinityTheme.panel
            border.color: TrinityTheme.border
            ListView {
                id: jobList
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: Trinity.jobModel
                spacing: 4
                property int selectedIndex: -1
                delegate: Rectangle {
                    required property var model
                    required property int index
                    width: ListView.view.width
                    height: 56
                    radius: TrinityTheme.radiusM
                    color: jobList.selectedIndex===index ? TrinityTheme.surface : (model.status==="FAILED" ? TrinityTheme.dangerBg : (model.status==="COMPLETED" ? TrinityTheme.successBg : TrinityTheme.panelRaised))
                    border.color: jobList.selectedIndex===index ? TrinityTheme.accentBright : (model.status==="FAILED" ? "#3A1E1A" : TrinityTheme.borderSoft)
                    MouseArea {
                        anchors.fill: parent
                        onClicked: jobList.selectedIndex = index
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label { text: model.jobId.substring(0,12) + "  " + model.engine + "." + model.operation; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: model.status + "  ·  " + Math.round(model.progress*100) + "%  ·  " + model.createdAt; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth: true }
                        }
                        ColumnLayout {
                            spacing: 3
                            StatusPill { status: model.status }
                            ProgressBar { from: 0; to: 1; value: model.progress; Layout.preferredWidth: 90; visible: model.status==="RUNNING"||model.status==="QUEUED" }
                        }
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }

        // Detail
        Rectangle {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 260
            radius: TrinityTheme.radiusM
            color: TrinityTheme.bg
            border.color: TrinityTheme.border
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                Label { text: jobList.selectedIndex>=0 ? ("JOB " + Trinity.jobModel.get(jobList.selectedIndex).jobId) : "Select a job"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle; wrapMode: Text.Wrap }
                Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft; visible: jobList.selectedIndex>=0 }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: jobList.selectedIndex>=0
                    clip: true
                    TextArea {
                        readOnly: true
                        wrapMode: Text.Wrap
                        color: TrinityTheme.textMuted
                        font.family: TrinityTheme.fontMono
                        font.pixelSize: 10
                        text: {
                            if (jobList.selectedIndex<0) return "";
                            return JSON.stringify(Trinity.jobModel.get(jobList.selectedIndex), null, 2);
                        }
                        background: Rectangle { color: TrinityTheme.panel; border.color: TrinityTheme.borderSoft; radius: TrinityTheme.radiusM }
                    }
                }
                Label { visible: jobList.selectedIndex<0; text: "Logs for the selected job appear in the bottom Logs tab. Progress streams via EventBus job.progress (queued signal, no blocking)."; color: TrinityTheme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                RowLayout {
                    visible: jobList.selectedIndex>=0
                    spacing: 6
                    property var rec: jobList.selectedIndex>=0 ? Trinity.jobModel.get(jobList.selectedIndex) : null
                    Button { text: "Pause"; enabled: rec && rec.status==="RUNNING"; onClicked: Trinity.jobModel.pauseJob(rec.jobId) }
                    Button { text: "Resume"; enabled: rec && rec.status==="PAUSED"; onClicked: Trinity.jobModel.resumeJob(rec.jobId) }
                    Button { text: "Cancel (Esc)"; enabled: rec && (rec.status==="RUNNING"||rec.status==="QUEUED"||rec.status==="PAUSED"); onClicked: Trinity.jobModel.cancelJob(rec.jobId) }
                }
            }
        }
    }
}
