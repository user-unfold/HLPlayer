import QtQuick
import QtQuick.Controls
import HLPlayer

Slider {
    id: root

    property color playedColor: ThemeManager.accentColor
    property color bufferedColor: Qt.rgba(1, 1, 1, 0.3)
    property color remainingColor: Qt.rgba(1, 1, 1, 0.15)
    property real bufferedValue: 0
    property int defaultHeight: 3
    property int hoverHeight: 5
    property bool showTooltip: true

    implicitHeight: 28

    background: Item {
        implicitHeight: root.defaultHeight

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: root.hovered ? root.hoverHeight : root.defaultHeight
            radius: height / 2
            color: root.remainingColor
            Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * root.bufferedValue
                height: parent.height
                radius: height / 2
                color: root.bufferedColor
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width * root.visualPosition
                height: parent.height
                radius: height / 2
                color: root.playedColor
            }
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.hovered || root.pressed ? 14 : 0
        height: width
        radius: width / 2
        color: "#FFFFFF"
        opacity: root.hovered || root.pressed ? 1.0 : 0.0
        Behavior on width { NumberAnimation { duration: 100 } }
        Behavior on opacity { NumberAnimation { duration: 100 } }

        layer.enabled: true
        layer.effect: DropShadow {
            radius: 4
            samples: 9
            color: "#40000000"
        }
    }
}
