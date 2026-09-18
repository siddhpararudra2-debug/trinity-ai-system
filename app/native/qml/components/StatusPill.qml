// Trinity — status pill: GENERATED/VALIDATED/VERIFIED/FAILED/SCAFFOLD
import QtQuick
import QtQuick.Controls
import "../theme"

Rectangle {
    property string status: "GENERATED"
    property bool isLive: false
    property string variant: "neutral" // neutral | success | warn | danger | scaffold | live

    function resolveVariant() {
        if (variant !== "neutral") return variant;
        const s = status.toUpperCase();
        if (s === "COMPLETED" || s === "VERIFIED" || s === "VALIDATED") return "success";
        if (s === "FAILED" || s === "CANCELLED") return "danger";
        if (s === "RUNNING" || s === "QUEUED" || s === "PAUSED") return "live";
        if (s === "SCAFFOLDED" || s === "SCAFFOLD") return "scaffold";
        return "neutral";
    }

    implicitWidth: label.implicitWidth + 16
    implicitHeight: 20
    radius: 3
    border.width: 1
    color: {
        const v = resolveVariant();
        if (v === "success") return TrinityTheme.successBg;
        if (v === "danger") return TrinityTheme.dangerBg;
        if (v === "scaffold") return TrinityTheme.scaffoldBg;
        if (v === "warn") return TrinityTheme.warningBg;
        if (v === "live") return TrinityTheme.accentSoft;
        return "transparent";
    }
    border.color: {
        const v = resolveVariant();
        if (v === "success") return "#2A5A3A";
        if (v === "danger") return "#5A2A22";
        if (v === "scaffold") return "#4A3D20";
        if (v === "warn") return "#4A3D20";
        if (v === "live") return TrinityTheme.accent;
        return TrinityTheme.border;
    }

    Label {
        id: label
        anchors.centerIn: parent
        text: status.toUpperCase()
        color: {
            const v = parent.resolveVariant();
            if (v === "success") return "#7FD8A0";
            if (v === "danger") return "#E88A7A";
            if (v === "scaffold") return "#C9B07A";
            if (v === "warn") return "#C9B07A";
            if (v === "live") return TrinityTheme.accentBright;
            return TrinityTheme.textMuted;
        }
        font.family: TrinityTheme.fontMono
        font.pixelSize: 9
        font.letterSpacing: 0.8
        font.bold: true
    }
}
