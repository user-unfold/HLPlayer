import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

ApplicationWindow {
    id: root
    width: 900
    height: 650
    visible: true
    title: qsTr("Camera Recording - HLPlayer")
    color: ThemeManager.surface

    onClosing: function(close) {
        if (recorder.isRecording) {
            recorder.stopRecording()
        }
    }

    QMLCameraRecorder {
        id: recorder

        Component.onCompleted: {
            enumerateCameras()
            enumerateMics()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Camera Recording")
            font.pixelSize: 24
            font.bold: true
            font.family: "IBM Plex Sans"
            color: ThemeManager.onSurface
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ThemeManager.onSurface
            opacity: 0.1
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Camera")
                    font.pixelSize: 12
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                ComboBox {
                    id: cameraCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    model: recorder.cameraList
                    onActivated: function(index) { recorder.selectedCamera = textAt(index) }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Microphone")
                    font.pixelSize: 12
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                ComboBox {
                    id: micCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    model: recorder.micList
                    onActivated: function(index) { recorder.selectedMic = textAt(index) }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 130
                spacing: 6

                Text {
                    text: qsTr("Resolution")
                    font.pixelSize: 12
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                ComboBox {
                    id: resolutionCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    model: ["1080p30", "720p30", "720p60", "480p30"]
                    currentIndex: 0
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ThemeManager.onSurface
            opacity: 0.1
        }

        // --- Stream section ---
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: qsTr("Stream Server")
                        font.pixelSize: 12
                        color: ThemeManager.onSurface
                        opacity: 0.7
                    }

                    TextField {
                        id: streamServerField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        placeholderText: qsTr("rtmp://a.rtmp.youtube.com/live2")
                        color: ThemeManager.onSurface
                        font.pixelSize: 13

                        background: Rectangle {
                            color: ThemeManager.surfaceVariant
                            radius: 6
                            border.color: streamServerField.activeFocus ? ThemeManager.accentColor : ThemeManager.onSurface
                            border.width: streamServerField.activeFocus ? 2 : 1
                            opacity: 0.5
                        }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 200
                    spacing: 6

                    Text {
                        text: qsTr("Stream Key")
                        font.pixelSize: 12
                        color: ThemeManager.onSurface
                        opacity: 0.7
                    }

                    TextField {
                        id: streamKeyField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        echoMode: TextInput.Password
                        placeholderText: qsTr("your-stream-key")
                        color: ThemeManager.onSurface
                        font.pixelSize: 13

                        background: Rectangle {
                            color: ThemeManager.surfaceVariant
                            radius: 6
                            border.color: streamKeyField.activeFocus ? ThemeManager.accentColor : ThemeManager.onSurface
                            border.width: streamKeyField.activeFocus ? 2 : 1
                            opacity: 0.5
                        }
                    }
                }
            }

            Text {
                text: streamServerField.text !== "" && streamKeyField.text !== ""
                      ? streamServerField.text + "/" + streamKeyField.text
                      : qsTr("Leave empty for file-only recording")
                font.pixelSize: 11
                color: ThemeManager.onSurface
                opacity: streamServerField.text !== "" && streamKeyField.text !== "" ? 0.8 : 0.4
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: ThemeManager.onSurface
            opacity: 0.1
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#1a1a1a"
            radius: 8

            Image {
                id: previewImage
                anchors.fill: parent
                anchors.margins: 2
                fillMode: Image.PreserveAspectFit
                source: recorder.isRecording ? "image://camera/preview?" + recorder.previewRefreshCounter : ""
                cache: false
            }

            Text {
                anchors.centerIn: parent
                text: recorder.isRecording ? "" : qsTr("Camera Preview")
                font.pixelSize: 18
                color: ThemeManager.onSurface
                opacity: 0.3
                visible: !recorder.isRecording
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 24

            Text {
                text: qsTr("Duration") + ": " + recorder.recordingDuration.toFixed(1) + "s"
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.onSurface
            }

            Text {
                text: qsTr("Size") + ": " + recorder.fileSize
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.onSurface
            }

            Text {
                text: qsTr("FPS") + ": " + recorder.fps
                font.pixelSize: 16
                font.bold: true
                color: ThemeManager.onSurface
            }

            Item { Layout.fillWidth: true }

            Button {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 48
                text: recorder.isPaused ? qsTr("Resume") : qsTr("Pause")
                visible: recorder.isRecording
                enabled: recorder.isRecording
                font.pixelSize: 16
                font.bold: true

                background: Rectangle {
                    radius: 8
                    color: recorder.isPaused ? "#2e7d32"
                           : parent.hovered ? "#e65100"
                           : "#ef6c00"
                }

                contentItem: Text {
                    anchors.centerIn: parent
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 16
                    font.bold: true
                }

                onClicked: {
                    if (recorder.isPaused) {
                        recorder.resumeRecording()
                    } else {
                        recorder.pauseRecording()
                    }
                }
            }

            Button {
                Layout.preferredWidth: 180
                Layout.preferredHeight: 48
                text: recorder.isRecording ? qsTr("Stop Recording") : qsTr("Start Recording")
                enabled: true
                font.pixelSize: 16
                font.bold: true

                background: Rectangle {
                    radius: 8
                    color: recorder.isRecording ? "#cc3333"
                           : parent.hovered ? ThemeManager.accentColor
                           : ThemeManager.primary
                }

                contentItem: Text {
                    anchors.centerIn: parent
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 16
                    font.bold: true
                }

                onClicked: {
                    if (recorder.isRecording) {
                        recorder.stopRecording()
                    } else {
                        var now = new Date()
                        var timestamp = now.getFullYear() + "-" +
                            String(now.getMonth() + 1).padStart(2, '0') + "-" +
                            String(now.getDate()).padStart(2, '0') + "_" +
                            String(now.getHours()).padStart(2, '0') + "-" +
                            String(now.getMinutes()).padStart(2, '0') + "-" +
                            String(now.getSeconds()).padStart(2, '0')
                        var outputPath = "D:/HLPlayer/recordings/recording_" + timestamp + ".mp4"
                        var streamUrl = streamServerField.text.trim()
                        var streamKey = streamKeyField.text.trim()

                        if (streamUrl !== "" && streamKey !== "") {
                            var fullStreamUrl = streamUrl.endsWith("/") ? streamUrl + streamKey : streamUrl + "/" + streamKey
                            var mode = 2  // Both: record + stream
                            recorder.startRecordingWithStream(outputPath, fullStreamUrl, mode,
                                1280, 720, 30, 2000000,
                                cameraCombo.currentText, micCombo.currentText)
                        } else {
                            recorder.startRecording(outputPath, 1280, 720, 30, 2000000,
                                                     cameraCombo.currentText, micCombo.currentText)
                        }
                    }
                }
            }
        }
    }
}
