import QtQuick
import QtQuick.Controls
import HLPlayer

Rectangle {
    id: root

    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: 12

    width: column.implicitWidth + 16
    height: column.implicitHeight + 12

    color: ThemeManager.surfaceVariant
    opacity: 0.9
    radius: 6

    property var player: null

    Behavior on opacity { NumberAnimation { duration: 200 } }

    Column {
        id: column
        anchors.centerIn: parent
        spacing: 4

        Row {
            spacing: 6
            anchors.right: parent.right

            Rectangle {
                id: connectionDot
                width: 8
                height: 8
                radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: {
                    if (!root.player) return "#666666"
                    var state = root.player.connectionState
                    if (state === "connected") return "#4CAF50"
                    if (state === "reconnecting") return "#FF9800"
                    return "#F44336"
                }

                Behavior on color { ColorAnimation { duration: 300 } }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (!root.player) return "Connection: --"
                    var state = root.player.connectionState
                    if (state === "connected") return "Connected"
                    if (state === "reconnecting") return "Reconnecting..."
                    return "Disconnected"
                }
                font.pixelSize: 11
                font.family: "IBM Plex Sans"
                color: ThemeManager.onSurface
            }
        }

        Text {
            anchors.right: parent.right
            text: root.player
                  ? "Bitrate: " + (root.player.streamBitrate > 0
                      ? (root.player.streamBitrate / 1000000).toFixed(2) + " Mbps"
                      : "-- Mbps")
                  : "Bitrate: -- Mbps"
            font.pixelSize: 11
            font.family: "IBM Plex Sans"
            color: ThemeManager.onSurface
        }

        Text {
            anchors.right: parent.right
            text: root.player
                  ? "Buffer: " + (root.player.bufferDuration > 0
                      ? root.player.bufferDuration + " ms"
                      : "-- ms")
                  : "Buffer: -- ms"
            font.pixelSize: 11
            font.family: "IBM Plex Sans"
            color: ThemeManager.onSurface
        }

        Text {
            anchors.right: parent.right
            text: root.player
                  ? "Dropped: " + root.player.droppedFrames
                  : "Dropped: --"
            font.pixelSize: 11
            font.family: "IBM Plex Sans"
            color: ThemeManager.onSurface
        }
    }
}
