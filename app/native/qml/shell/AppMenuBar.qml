// Trinity — native menu bar
import QtQuick
import QtQuick.Controls
import "../theme"

MenuBar {
    // Forward signals to parent window's handlers
    signal newProjectRequested()
    signal openProjectRequested()
    signal importProjectRequested()
    signal exportProjectRequested()
    signal saveRequested()
    signal exportRequested()
    signal undoRequested()
    signal redoRequested()
    signal paletteRequested()
    signal runRequested()
    signal validateRequested()
    signal cancelRequested()
    signal layoutResetRequested()
    signal settingsRequested()
    signal aboutRequested()

    Menu {
        title: qsTr("&File")
        Action { text: qsTr("New Project…\tCtrl+N"); onTriggered: newProjectRequested() }
        Action { text: qsTr("Open Project…\tCtrl+O"); onTriggered: openProjectRequested() }
        MenuSeparator {}
        Action { text: qsTr("Import Project…"); onTriggered: importProjectRequested() }
        Action { text: qsTr("Export Project…\tCtrl+Shift+S"); onTriggered: exportRequested() }
        MenuSeparator {}
        Action { text: qsTr("Save\tCtrl+S"); onTriggered: saveRequested() }
        MenuSeparator {}
        Action { text: qsTr("Quit"); onTriggered: Qt.quit() }
    }
    Menu {
        title: qsTr("&Edit")
        Action { text: qsTr("Undo\tCtrl+Z"); onTriggered: undoRequested() }
        Action { text: qsTr("Redo\tCtrl+Y"); onTriggered: redoRequested() }
    }
    Menu {
        title: qsTr("&View")
        Action { text: qsTr("Command Palette\tCtrl+K"); onTriggered: paletteRequested() }
        MenuSeparator {}
        Action { text: qsTr("Toggle Left Panel"); onTriggered: Trinity.layout.toggleLeft() }
        Action { text: qsTr("Toggle Right Panel"); onTriggered: Trinity.layout.toggleRight() }
        Action { text: qsTr("Toggle Bottom Panel"); onTriggered: Trinity.layout.toggleBottom() }
        MenuSeparator {}
        Action { text: qsTr("Restore Layout"); onTriggered: layoutResetRequested() }
    }
    Menu {
        title: qsTr("&Project")
        Action { text: qsTr("Run\tF5"); onTriggered: runRequested() }
        Action { text: qsTr("Validate\tF6"); onTriggered: validateRequested() }
        Action { text: qsTr("Cancel\tEsc"); onTriggered: cancelRequested() }
    }
    Menu {
        title: qsTr("&Tools")
        Action { text: qsTr("Settings…"); onTriggered: settingsRequested() }
        Action { text: qsTr("Job Monitor"); onTriggered: Trinity.workspace.switchTo("jobs") }
    }
    Menu {
        title: qsTr("&Help")
        Action { text: qsTr("About Trinity"); onTriggered: aboutRequested() }
        Action { text: qsTr("Engine Registry"); onTriggered: Trinity.workspace.switchTo("cad") }
    }
}
