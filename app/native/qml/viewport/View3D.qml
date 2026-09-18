// Trinity — engineering 3D viewport (native).
// Uses a procedural preview until QtQuick3D mesh pipeline lands. Live artifact
// selection drives real stats via ViewportController — no fake geometry.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: viewport
    color: "#090B0D"
    border.color: TrinityTheme.border

    // Expose for parent loader
    property alias showGrid: gridToggle.checked
    property alias wireframe: wireToggle.checked

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        // Top bar: title + view cube controls
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "TRINITY / VIEWPORT"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10; font.letterSpacing: 1.1 }
            Label { text: "· " + Trinity.viewport.viewPreset; color: TrinityTheme.accentBright; font.family: TrinityTheme.fontMono; font.pixelSize: 10; Layout.leftMargin: 6 }
            Item { Layout.fillWidth: true }
            Repeater {
                model: ["FRONT","TOP","RIGHT","ISO","FIT"]
                Button {
                    required property string modelData
                    text: modelData
                    flat: true
                    highlighted: Trinity.viewport.viewPreset === modelData
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 10
                    onClicked: Trinity.viewport.setView(modelData)
                }
            }
            ToolSeparator {}
            CheckBox { id: gridToggle; checked: Trinity.viewport.showGrid; text: "Grid"; font.pixelSize: 11; onToggled: Trinity.viewport.showGrid = checked }
            CheckBox { id: wireToggle; checked: Trinity.viewport.wireframe; text: "Wire"; font.pixelSize: 11; onToggled: Trinity.viewport.wireframe = checked }
            CheckBox { checked: Trinity.viewport.showAxes; text: "Axes"; font.pixelSize: 11; onToggled: Trinity.viewport.showAxes = checked }
            CheckBox { checked: Trinity.viewport.ortho; text: "Ortho"; font.pixelSize: 11; onToggled: Trinity.viewport.ortho = checked }
            CheckBox { checked: Trinity.viewport.spinning; text: "Spin"; font.pixelSize: 11; onToggled: Trinity.viewport.spinning = checked }
        }

        // Canvas surface — procedural preview (replaced by View3D mesh pipeline in next increment)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: TrinityTheme.radiusM
            color: "#0E1113"
            border.color: TrinityTheme.borderSoft
            clip: true

            // Grid overlay (procedural)
            Canvas {
                id: gridCanvas
                anchors.fill: parent
                visible: Trinity.viewport.showGrid
                opacity: 0.35
                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = "#1E2A31";
                    ctx.lineWidth = 1;
                    const step = 28;
                    for (let x=0; x<width; x+=step) { ctx.beginPath(); ctx.moveTo(x,0); ctx.lineTo(x,height); ctx.stroke(); }
                    for (let y=0; y<height; y+=step) { ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(width,y); ctx.stroke(); }
                    // center cross
                    ctx.strokeStyle = "#2A3D4A";
                    ctx.beginPath(); ctx.moveTo(width/2,0); ctx.lineTo(width/2,height); ctx.stroke();
                    ctx.beginPath(); ctx.moveTo(0,height/2); ctx.lineTo(width,height/2); ctx.stroke();
                }
                Component.onCompleted: requestPaint()
                Connections { target: Trinity.viewport; function onDisplayChanged(){ gridCanvas.requestPaint() } }
            }

            // Center preview
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 14
                width: Math.min(parent.width*0.86, 520)
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 86; height: 86; radius: 10
                    color: TrinityTheme.surface
                    border.color: TrinityTheme.border
                    Label {
                        anchors.centerIn: parent
                        text: Trinity.viewport.wireframe ? "◇" : "⬢"
                        color: TrinityTheme.text
                        font.pixelSize: 36
                    }
                    // Axes indicator
                    Row {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: -8
                        spacing: 4
                        visible: Trinity.viewport.showAxes
                        Rectangle { width: 18; height: 3; color: "#E05040"; radius: 1 }
                        Rectangle { width: 18; height: 3; color: "#40C060"; radius: 1 }
                        Rectangle { width: 18; height: 3; color: "#4A90E0"; radius: 1 }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Trinity.viewport.selectedArtifactId.length>0 ? "ARTIFACT " + Trinity.viewport.selectedArtifactId.substring(0,8) : "NO GEOMETRY LOADED"
                    color: Trinity.viewport.selectedArtifactId.length>0 ? TrinityTheme.text : TrinityTheme.textDim
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 11
                    font.letterSpacing: 1.0
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Trinity.viewport.selectedArtifactId.length>0
                          ? ("triangles " + (Trinity.viewport.meshStats.triangles||0) + " · vertices " + (Trinity.viewport.meshStats.vertices||0) + " · " + (Trinity.viewport.meshStats.sizeMm||0) + " mm  ·  " + Trinity.viewport.statusText)
                          : "Select an artifact or generate CAD to populate the viewport. Mesh render pipeline is staged — this surface shows live job/geometry metadata."
                    color: TrinityTheme.textFaint
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    lineHeight: 1.35
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    Button { text: "Generate 50 mm"; onClicked: Trinity.run_command_async("create a 50 mm quadcopter frame") }
                    Button { text: "Fit"; onClicked: Trinity.viewport.fitView() }
                    Button { text: "Reset"; onClicked: Trinity.viewport.resetView() }
                }
            }

            // Corner brackets
            Rectangle { anchors.fill: parent; color: "transparent"; border.color: "transparent"
                Canvas {
                    anchors.fill: parent
                    onPaint: {
                        const ctx=getContext("2d"); ctx.reset(); ctx.strokeStyle="#2A3A45"; ctx.lineWidth=1.2;
                        const s=14, m=8;
                        // TL
                        ctx.beginPath(); ctx.moveTo(m,m+s); ctx.lineTo(m,m); ctx.lineTo(m+s,m); ctx.stroke();
                        // TR
                        ctx.beginPath(); ctx.moveTo(width-m-s,m); ctx.lineTo(width-m,m); ctx.lineTo(width-m,m+s); ctx.stroke();
                        // BL
                        ctx.beginPath(); ctx.moveTo(m,height-m-s); ctx.lineTo(m,height-m); ctx.lineTo(m+s,height-m); ctx.stroke();
                        // BR
                        ctx.beginPath(); ctx.moveTo(width-m-s,height-m); ctx.lineTo(width-m,height-m); ctx.lineTo(width-m,height-m-s); ctx.stroke();
                    }
                    Component.onCompleted: requestPaint()
                }
            }

            // HUD spec
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 10
                width: 180
                radius: TrinityTheme.radiusM
                color: Qt.rgba(0.08,0.10,0.12,0.82)
                border.color: TrinityTheme.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    Label { text: "SPEC — PREVIEW"; color: TrinityTheme.accentBright; font.family: TrinityTheme.fontMono; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.fillWidth: true }
                    Rectangle { Layout.fillWidth: true; height: 1; color: TrinityTheme.borderSoft }
                    RowLayout { Layout.fillWidth: true; Label{ text:"Overall"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth:true} Label{ text:(Trinity.viewport.meshStats.sizeMm||50)+" mm"; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 10 } }
                    RowLayout { Layout.fillWidth: true; Label{ text:"Triangles"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth:true} Label{ text: Trinity.viewport.meshStats.triangles||0; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 10 } }
                    RowLayout { Layout.fillWidth: true; Label{ text:"Projection"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9; Layout.fillWidth:true} Label{ text: Trinity.viewport.ortho?"ORTHO":"PERSPECTIVE"; color: TrinityTheme.text; font.family: TrinityTheme.fontMono; font.pixelSize: 10 } }
                }
            }

            // Bottom legend
            RowLayout {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 10
                spacing: 12
                Label { text: Trinity.viewport.ortho ? "orthographic" : "perspective"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
                Item { Layout.fillWidth: true }
                Label { text: Trinity.viewport.wireframe ? "wireframe" : "solid"; color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
                Label { text: "·"; color: TrinityTheme.textDim }
                Label { text: "orbit: drag  ·  zoom: wheel  ·  fit: FIT"; color: TrinityTheme.textDim; font.family: TrinityTheme.fontMono; font.pixelSize: 9 }
            }
        }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label { text: (Trinity.viewport.meshStats.sizeMm||50).toFixed(1) + " × " + (Trinity.viewport.meshStats.sizeMm||50).toFixed(1) + " MM"; color: TrinityTheme.textMuted; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            Item { Layout.fillWidth: true }
            Label { text: "TRIANGLES: " + (Trinity.viewport.meshStats.triangles||0); color: TrinityTheme.textFaint; font.family: TrinityTheme.fontMono; font.pixelSize: 10 }
            StatusPill { status: Trinity.viewport.selectedArtifactId.length>0 ? "VALIDATED" : "AWAITING"; variant: Trinity.viewport.selectedArtifactId.length>0 ? "success" : "neutral" }
        }
    }
}
