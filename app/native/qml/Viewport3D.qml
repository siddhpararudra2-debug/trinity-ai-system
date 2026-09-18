// Trinity — engineering 3D viewport (foundation build).
// The production mesh renderer plugs in here; this panel establishes the
// control surface: view cube shortcuts, display modes, grid/axes toggles,
// fit-to-view, and the object tree hookup. Rendering is staged: the panel
// currently shows live job/geometry metadata until the Scene3D mesh pipeline
// lands — it does NOT pretend to render geometry it does not have.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: viewport
    color: "#0d1013"
    border.color: "#222a30"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Label { text: "TRINITY / VIEWPORT"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }
            Item { Layout.fillWidth: true }
            // Standard engineering views.
            Repeater {
                model: ["FRONT", "TOP", "RIGHT", "ISO", "FIT"]
                Button {
                    required property string modelData
                    text: modelData
                    flat: true
                    onClicked: console.log("viewport view:", modelData)
                }
            }
            Switch { id: gridToggle; checked: true; text: qsTr("Grid") }
            Switch { id: wireToggle; text: qsTr("Wireframe") }
        }

        // Staging surface. The Scene3D mesh pipeline replaces this placeholder.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            Column {
                anchors.centerIn: parent
                spacing: 8
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "MESH RENDER PIPELINE: STAGED"
                    color: "#5a6a74"; font.pixelSize: 12; font.letterSpacing: 2
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "STL artifacts are stored and validated by the core.\nViewport orbit/pan/zoom + grid attach here."
                    color: "#3d4a52"; horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "orthographic"; color: "#5a6a74"; font.pixelSize: 11 }
            Switch { id: orthoToggle }
            Item { Layout.fillWidth: true }
            Label { text: "measurement: foundation stub"; color: "#3d4a52"; font.pixelSize: 10 }
        }
    }
}
