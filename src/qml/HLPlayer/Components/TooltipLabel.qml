import QtQuick
import HLPlayer

Rectangle {
    id: root

    property string text: ""
    property point anchorPoint: Qt.point(0, 0)
    property real maxWidth: 200

    visible: text !== "" && opacity > 0
    width: Math.min(tipText.implicitWidth + 12, maxWidth)
    height: tipText.implicitHeight + 8
    radius: 4
    color: ThemeManager.controlBarBackstop
    opacity: text !== "" ? 1.0 : 0.0

    Behavior on opacity { NumberAnimation { duration: 100 } }

    Text {
        id: tipText
        anchors.centerIn: parent
        text: root.text
        color: ThemeManager.textPrimary
        font.pixelSize: 11
        font.family: "IBM Plex Sans"
        elide: Text.ElideRight
    }
}
