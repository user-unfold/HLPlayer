import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

Rectangle {
    id: root

    color: ThemeManager.surface

    property alias player: innerPlayer

    QMLPlayer {
        id: innerPlayer
    }

    Rectangle {
        id: videoArea
        anchors.fill: parent
        color: "#000000"

        Rectangle {
            anchors.centerIn: parent
            width: 64
            height: 64
            radius: 32
            color: ThemeManager.onSurface
            opacity: root.player.isPlaying ? 0.0 : 0.7
            visible: opacity > 0.01

            Behavior on opacity { NumberAnimation { duration: 200 } }

            Text {
                anchors.centerIn: parent
                text: "\u25B6"
                font.pixelSize: 28
                color: ThemeManager.surface
            }
        }

        Text {
            anchors.centerIn: parent
            text: root.player.error !== "" ? root.player.error : ""
            color: ThemeManager.errorColor
            font.pixelSize: 14
            visible: root.player.state === 6
        }

        MouseArea {
            anchors.fill: parent
            onClicked: function() {
                if (root.player.isPlaying) {
                    root.player.pause()
                } else {
                    root.player.play()
                }
            }
        }
    }

    ControlsBar {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        player: root.player
    }
}
