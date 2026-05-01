import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var bridge: null
    color: "#2a2a2a"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16

        Text {
            text: "Parameters"
            font.pixelSize: 16
            font.bold: true
            color: "#ffffff"
        }

        Label { text: "Scale"; color: "#cccccc" }
        ComboBox {
            Layout.fillWidth: true
            model: ["2x", "3x", "4x"]
            onActivated: if (bridge) bridge.setScaleFactor(parseFloat(currentText.replace("x", "")))
        }

        Label { text: "Encoder"; color: "#cccccc" }
        ComboBox {
            Layout.fillWidth: true
            model: ["H264", "HEVC", "AV1"]
            onActivated: if (bridge) bridge.setEncoder(currentText)
        }

        Label { text: "Quality"; color: "#cccccc" }
        Slider {
            Layout.fillWidth: true
            from: 0
            to: 100
            value: 80
            onMoved: if (bridge) bridge.setQuality(value)
        }

        Label { text: "Preset"; color: "#cccccc" }
        ComboBox {
            Layout.fillWidth: true
            model: ["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"]
            onActivated: if (bridge) bridge.setPreset(currentText)
        }

        Label { text: "Performance"; color: "#cccccc" }
        ComboBox {
            Layout.fillWidth: true
            model: ["Performance", "Balanced"]
            onActivated: if (bridge) bridge.setPerformanceMode(currentText)
        }

        Item { Layout.fillHeight: true }
    }
}
