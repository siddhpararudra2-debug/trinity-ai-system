// Trinity — command console: deterministic engineering command entry.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: console_root
    color: "#101418"
    border.color: "#222a30"

    signal commandIssued(string text)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Label { text: "COMMAND / ENGINE CONSOLE"; color: "#8a9aa4"; font.pixelSize: 11; font.letterSpacing: 1.5 }

        RowLayout {
            Layout.fillWidth: true
            Label { text: "❯"; color: "#4fd6e5"; font.bold: true }
            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: "create a 50 mm quadcopter frame   |   calculate 2*pi*25   |   help"
                font.family: "Consolas"
                color: "#dfe8ec"
                background: Rectangle { color: "#0a0d10"; border.color: "#2a343b" }
                onAccepted: {
                    if (text.trim().length === 0) return
                    console_root.commandIssued(text.trim())
                    historyModel.insert(0, text.trim())
                    text = ""
                }
                ListModel { id: historyModel }
            }
            Button { text: "run"; onClicked: input.accepted() }
        }
    }
}
