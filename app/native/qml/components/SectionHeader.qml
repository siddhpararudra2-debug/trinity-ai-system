// Trinity — section label: 01 / LABEL pattern
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

RowLayout {
    property string number: "01"
    property string label: "SECTION"
    property string detail: ""
    spacing: 10
    Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 1; color: TrinityTheme.borderStrong; Layout.alignment: Qt.AlignVCenter }
    Label {
        text: number + " / " + label
        color: TrinityTheme.textFaint
        font.family: TrinityTheme.fontMono
        font.pixelSize: 10
        font.letterSpacing: 1.1
    }
    Label {
        visible: detail.length > 0
        text: detail
        color: TrinityTheme.textDim
        font.family: TrinityTheme.fontMono
        font.pixelSize: 10
    }
    Item { Layout.fillWidth: true }
}
