pragma Singleton
import QtQuick
import HLPlayer

QtObject {
    readonly property color accent:            ThemeManager.accentColor
    readonly property color secondaryAccent:   ThemeManager.secondaryAccent
    readonly property color bgMain:            ThemeManager.bgMain
    readonly property color bgSidebar:         ThemeManager.bgSidebar
    readonly property color textPrimary:       ThemeManager.textPrimary
    readonly property color textSecondary:     ThemeManager.textSecondary
    readonly property color textDisabled:      ThemeManager.textDisabled
    readonly property color border:            ThemeManager.border
    readonly property color hoverHighlight:    ThemeManager.hoverHighlight
    readonly property color controlBarBackstop:ThemeManager.controlBarBackstop
    readonly property color error:             ThemeManager.errorColor

    readonly property color progressPlayed:    accent
    readonly property color progressBuffered:  Qt.rgba(1, 1, 1, 0.3)
    readonly property color progressRemaining: Qt.rgba(1, 1, 1, 0.15)

    readonly property int radiusButton:  6
    readonly property int radiusCard:   10
    readonly property int radiusDialog: 14

    readonly property int animDurationFast:  150
    readonly property int animDurationMedium:200
    readonly property int animDurationSlow:  300
}
