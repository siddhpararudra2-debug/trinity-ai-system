// Trinity — scaffold banner: honest unavailable state, never faked.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    property string engineId: ""
    property string detail: ""
    property string badge: "SCAFFOLD · 501"

    color: TrinityTheme.scaffoldBg
    border.color: "#4A3D20"
    radius: TrinityTheme.radiusM
    implicitHeight: col.implicitHeight + 24

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 14
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                width: 6; height: 6; radius: 3
                color: TrinityTheme.warning
            }
            Label {
                text: "EXPERIMENTAL — NOT IMPLEMENTED"
                color: "#C9B07A"
                font.family: TrinityTheme.fontMono
                font.pixelSize: 10
                font.letterSpacing: 1.2
                font.bold: true
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: badgeLabel.implicitWidth + 14
                implicitHeight: 20
                radius: 3
                color: "transparent"
                border.color: "#4A3D20"
                Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: badge
                    color: "#C9B07A"
                    font.family: TrinityTheme.fontMono
                    font.pixelSize: 9
                    font.letterSpacing: 0.6
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: detail.length > 0 ? detail : (engineId.length > 0 ? "The " + engineId + " engine is scaffolded and cannot execute this operation yet (capability_unavailable)." : "This capability is scaffolded. Architecture is registered; execution is not implemented.")
            color: TrinityTheme.textMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            lineHeight: 1.4
        }
        Label {
            Layout.fillWidth: true
            text: "Track in: Engine Registry → health: scaffolded · Use CAD / Math / Artifacts / Jobs for verified execution."
            color: TrinityTheme.textFaint
            font.family: TrinityTheme.fontMono
            font.pixelSize: 10
            wrapMode: Text.Wrap
        }
    }
}
