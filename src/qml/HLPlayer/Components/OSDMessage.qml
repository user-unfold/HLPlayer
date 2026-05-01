import QtQuick
import HLPlayer

Rectangle {
    id: root

    property string iconText: ""
    property string message: ""
    property int displayDuration: 2000
    property alias textColor: osdText.color

    signal dismissed()

    width: osdRow.implicitWidth + 32
    height: osdRow.implicitHeight + 20
    color: ThemeManager.controlBarBackstop
    radius: 8
    opacity: 0.0
    visible: opacity > 0

    Row {
        id: osdRow
        anchors.centerIn: parent
        spacing: 10

        Text {
            text: root.iconText
            font.pixelSize: 22
            color: ThemeManager.textPrimary
            visible: root.iconText !== ""
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: osdText
            text: root.message
            font.pixelSize: 16
            font.bold: true
            color: ThemeManager.textPrimary
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    function show(msg, icon) {
        if (icon !== undefined) root.iconText = icon
        root.message = msg || ""
        root.opacity = 0.0
        fadeInAnimation.start()
    }

    function dismiss() {
        fadeOutAnimation.start()
    }

    NumberAnimation {
        id: fadeInAnimation
        target: root
        property: "opacity"
        to: 0.9
        duration: 150
        easing.type: Easing.OutCubic
        onStopped: {
            if (root.opacity > 0) dismissTimer.restart()
        }
    }

    NumberAnimation {
        id: fadeOutAnimation
        target: root
        property: "opacity"
        to: 0.0
        duration: 400
        easing.type: Easing.InCubic
        onStopped: root.dismissed()
    }

    Timer {
        id: dismissTimer
        interval: root.displayDuration
        onTriggered: fadeOutAnimation.start()
    }
}
