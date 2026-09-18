// Trinity — global command palette (Ctrl+K).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: palette
    width: 560
    height: 380
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    background: Rectangle { color: "#141a1f"; border.color: "#2a343b"; radius: 6 }

    // Commands: New/Open/Import/Export Project, Run Engine, Create CAD,
    // Calculate, Import/Export Artifact, Validate, View Jobs, Settings, Search.
    property var commands: [
        { label: "New Project", action: function() { Trinity.create_project("Untitled", "") } },
        { label: "Open Project…", action: function() { console.log("open project") } },
        { label: "Import Project…", action: function() { console.log("import") } },
        { label: "Export Project…", action: function() { console.log("export") } },
        { label: "Create CAD — quadcopter frame", action: function() { Trinity.run_command("create a 50 mm quadcopter frame") } },
        { label: "Calculate — evaluate expression", action: function() { console.log("calculate") } },
        { label: "Validate Artifact", action: function() { console.log("validate") } },
        { label: "View Jobs", action: function() { console.log("view jobs") } },
        { label: "Search Research", action: function() { console.log("search") } },
        { label: "Open Settings…", action: function() { settingsDialog.open() } },
        { label: "Open Console", action: function() { console.log("console") } }
    ]

    contentItem: ColumnLayout {
        spacing: 6
        TextField {
            id: filter
            Layout.fillWidth: true
            placeholderText: "Type a command…"
            font.family: "Consolas"
            onTextChanged: list.model = palette.commands.filter(function(c) {
                return c.label.toLowerCase().indexOf(text.toLowerCase()) !== -1
            })
            Component.onCompleted: forceActiveFocus()
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: palette.commands
            delegate: ItemDelegate {
                required property var modelData
                width: list.width
                text: modelData.label
                highlighted: ListView.isCurrentItem
                onClicked: { modelData.action(); palette.close() }
            }
        }
    }
}
