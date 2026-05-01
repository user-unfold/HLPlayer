import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var bridge: null
    color: "#000000"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1a1a"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text { text: "Original"; color: "#ffffff"; font.pixelSize: 14 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0a0a0a"
                    border.color: "#333333"
                    Image {
                        id: originalImage
                        anchors.centerIn: parent
                        fillMode: Image.PreserveAspectFit
                        source: "image://preview/original"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1a1a"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text { text: "Enhanced"; color: "#ffffff"; font.pixelSize: 14 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#0a0a0a"
                    border.color: "#333333"
                    Image {
                        id: enhancedImage
                        anchors.centerIn: parent
                        fillMode: Image.PreserveAspectFit
                        source: "image://preview/enhanced"
                    }
                }
            }
        }
    }

    Connections {
        target: root.bridge
        function onPreviewFrameReady(frameData, width, height, isOriginal) {
            if (isOriginal) originalImage.source = "data:image/png;base64," + frameData
            else enhancedImage.source = "data:image/png;base64," + frameData
        }
    }
}
