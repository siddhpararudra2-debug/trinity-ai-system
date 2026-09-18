// Trinity — design tokens: professional engineering workstation.
// Dark, low-chroma, hairline borders. No saturated gradients, no glassmorphism.
pragma Singleton
import QtQuick

QtObject {
    // Appearance binding (live via Settings — default dark workstation)
    property string theme: "dark" // dark | light
    property bool isLight: theme === "light"
    // Palette — dark workstation default, light paper option (#F7F7F4) via Settings → Appearance
    property color bg: isLight ? "#F7F7F4" : "#0C0E10"
    property color bgElevated: isLight ? "#F1F1ED" : "#121518"
    property color panel: isLight ? "#FFFFFF" : "#14181B"
    property color panelRaised: isLight ? "#F1F1ED" : "#191E22"
    property color surface: isLight ? "#E8EEF2" : "#1A2126"
    property color border: isLight ? "#D0D0CA" : "#242D33"
    property color borderStrong: isLight ? "#B8B8B2" : "#2E3A43"
    property color borderSoft: isLight ? "#E6E6E0" : "#1E262C"
    property color text: isLight ? "#111111" : "#E6EEF2"
    property color textMuted: isLight ? "#5A5A56" : "#8EA0AE"
    property color textFaint: isLight ? "#707070" : "#607080"
    property color textDim: isLight ? "#9A9A94" : "#45565F"
    property color accent: "#2A5A73"
    property color accentSoft: isLight ? "#E8EEF2" : "#1E3D4F"
    property color accentBright: "#3A7AA0"
    property color success: "#2D6A4F"
    property color successBg: isLight ? "#EDF5EF" : "#0F1F18"
    property color warning: "#8A6D3B"
    property color warningBg: isLight ? "#FDF6E3" : "#1F1A0F"
    property color danger: "#8B3A30"
    property color dangerBg: isLight ? "#FDF0F0" : "#1F1412"
    property color scaffold: "#6B5A30"
    property color scaffoldBg: isLight ? "#1A160F" : "#1A160F"
    // Font scale live (1.0 = 100%)
    property real fontScale: 1.0

    // Spacing
    readonly property int spaceXS: 4
    readonly property int spaceS: 8
    readonly property int spaceM: 12
    readonly property int spaceL: 16
    readonly property int spaceXL: 24
    readonly property int space2XL: 32

    // Radii
    readonly property int radiusS: 3
    readonly property int radiusM: 5
    readonly property int radiusL: 8

    // Typography
    readonly property string fontMono: "Cascadia Code, JetBrains Mono, Consolas, monospace"
    readonly property string fontSans: "Inter, Segoe UI, sans-serif"
    readonly property string fontDisplay: "Space Grotesk, Segoe UI, sans-serif"

    // Motion (subtle only)
    readonly property int durationFast: 120
    readonly property int durationNormal: 180
    readonly property int durationSlow: 260
    // Easing curve for dock/state transitions
    // Use in QML: Behavior on width { NumberAnimation { duration: TrinityTheme.durationNormal; easing.type: Easing.InOutCubic } }

    // Layout
    readonly property int topBarHeight: 38
    readonly property int menuHeight: 28
    readonly property int leftMin: 200
    readonly property int leftMax: 420
    readonly property int rightMin: 260
    readonly property int rightMax: 480
    readonly property int bottomMin: 140
    readonly property int bottomMax: 520

    // Hairline
    readonly property int hairline: 1
}
