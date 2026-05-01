import QtQuick
import QtQuick.Layouts
import HLPlayer

Rectangle {
    id: root

    property var player: null
    property var formatTimeFunc: null

    width: infoColumn.implicitWidth + 24
    height: infoColumn.implicitHeight + 20
    color: Qt.rgba(0, 0, 0, 0.75)
    radius: 10

    visible: false

    onVisibleChanged: {
        if (visible && root.player) vInfoTimer.start()
    }

    Timer {
        id: vInfoTimer; interval: 500; repeat: true
        onTriggered: {
            if (root.player) {
                posText.text = root.formatTimeFunc ? root.formatTimeFunc(root.player.position) : ""
            }
        }
    }

    Column {
        id: infoColumn
        anchors.left: parent.left; anchors.top: parent.top
        anchors.margins: 12
        spacing: 4
        width: implicitWidth

        Text { text: root.player && root.player.source ? root.player.source.split("/").pop() : "--"; font.pixelSize: 14; font.bold: true; color: "#ffffff"; font.family: "Consolas" }
        Text { text: root.player ? root.player.videoWidth + "x" + root.player.videoHeight + " @ " + root.player.fps.toFixed(1) + " fps" : "--"; font.pixelSize: 12; color: "#cccccc"; font.family: "Consolas" }
        Text { text: root.player && root.player.streamBitrate > 0 ? "Bitrate: " + (root.player.streamBitrate / 1000000).toFixed(2) + " Mbps" : "Bitrate: --"; font.pixelSize: 12; color: "#cccccc"; font.family: "Consolas" }
        Text { id: posText; text: root.formatTimeFunc && root.player ? root.formatTimeFunc(root.player.position) : ""; font.pixelSize: 12; color: ThemeManager.accentColor; font.family: "Consolas" }
    }
}
