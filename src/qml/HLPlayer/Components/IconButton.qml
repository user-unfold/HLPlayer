import QtQuick
import QtQuick.Controls
import HLPlayer

Button {
    id: root
    flat: true

    property string iconText: ""
    property int iconSize: 16
    property int buttonSize: 36
    property bool isAccent: false
    property color customColor: "#CCCCCC"
    property int radius: 6

    implicitWidth: buttonSize
    implicitHeight: buttonSize

    background: Rectangle {
        radius: root.radius
        color: root.hovered
               ? (root.isAccent ? "#8B5CF6" : Qt.rgba(1, 1, 1, 0.08))
               : "transparent"
        opacity: root.hovered ? (root.isAccent ? 0.25 : 1.0) : 0.0
        Behavior on opacity { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }

    contentItem: Text {
        text: root.iconText
        font.pixelSize: root.iconSize
        color: root.hovered
               ? "#FFFFFF"
               : (root.isAccent ? "#8B5CF6" : root.customColor)
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        Behavior on color { ColorAnimation { duration: 150; easing.type: Easing.OutCubic } }
    }

    Accessible.role: Accessible.Button
    Accessible.name: root.iconText
}
