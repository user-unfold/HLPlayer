import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var bridge: null
    color: "#2a2a2a"
    height: 32

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: 1
            value: root.bridge ? root.bridge.progress : 0
            background: Rectangle { color: "#1a1a1a"; radius: 2 }
            contentItem: Rectangle {
                width: parent.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: "#4a9eff"
            }
        }

        Text {
            text: "FPS: " + (root.bridge ? root.bridge.currentFps.toFixed(1) : "0.0")
            color: "#cccccc"
            font.pixelSize: 12
        }

        Text {
            text: "ETA: " + (root.bridge ? formatTime(root.bridge.estimatedTimeRemaining) : "--:--")
            color: "#cccccc"
            font.pixelSize: 12
        }

        Text {
            text: "Status: " + (root.bridge ? root.bridge.currentStatus : "Idle")
            color: "#cccccc"
            font.pixelSize: 12
        }

        Text {
            text: "VRAM: " + (root.bridge ? formatBytes(root.bridge.vramUsedBytes) : "0 MB")
            color: "#cccccc"
            font.pixelSize: 12
        }

        Text {
            text: (root.bridge ? root.bridge.framesProcessed : 0) + "/" + (root.bridge ? root.bridge.totalFrames : 0)
            color: "#cccccc"
            font.pixelSize: 12
        }
    }

    function formatTime(seconds) {
        if (seconds <= 0) return "--:--"
        var m = Math.floor(seconds / 60)
        var s = Math.floor(seconds % 60)
        return m + "m " + s + "s"
    }

    function formatBytes(bytes) {
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        return (bytes / (1024 * 1024)).toFixed(1) + " MB"
    }
}
