import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

ApplicationWindow {
    id: root
    width: 800
    height: 500
    visible: true
    title: qsTr("Streaming - HLPlayer")
    color: ThemeManager.surface

    QMLCameraRecorder {
        id: recorder
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        Text {
            text: qsTr("Media Streaming")
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
                    text: qsTr("Stream URL")
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                TextField {
                    id: urlField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: qsTr("rtmp://server/app/key")
                    color: ThemeManager.onSurface
                    font.pixelSize: 14
                    font.family: "IBM Plex Sans"

                    background: Rectangle {
                        color: ThemeManager.surfaceVariant
                        radius: 6
                        border.color: urlField.activeFocus ? ThemeManager.accentColor : ThemeManager.onSurface
                        border.width: urlField.activeFocus ? 2 : 1
                        opacity: 0.5
                    }

                    onTextChanged: {
                        updateProtocolDetection()
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: false
                spacing: 6

                Text {
                    text: qsTr("Protocol")
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.preferredHeight: 40
                    color: ThemeManager.surfaceVariant
                    radius: 6
                    border.color: ThemeManager.onSurface
                    border.width: 1
                    opacity: 0.5

                    Text {
                        anchors.centerIn: parent
                        text: protocolLabel
                        font.pixelSize: 14
                        font.bold: true
                        font.family: "IBM Plex Sans"
                        color: protocolColor
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Source File")
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                TextField {
                    id: sourceField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    placeholderText: qsTr("Select MP4 file")
                    readOnly: true
                    color: ThemeManager.onSurface
                    font.pixelSize: 14
                    font.family: "IBM Plex Sans"

                    background: Rectangle {
                        color: ThemeManager.surfaceVariant
                        radius: 6
                        border.color: ThemeManager.onSurface
                        border.width: 1
                        opacity: 0.5
                    }
                }
            }

            Button {
                Layout.preferredWidth: 100
                Layout.preferredHeight: 40
                text: qsTr("Browse")
                font.pixelSize: 13
                font.family: "IBM Plex Sans"

                background: Rectangle {
                    radius: 6
                    color: parent.pressed ? ThemeManager.accentColor
                           : parent.hovered ? ThemeManager.accentColor
                           : ThemeManager.surfaceVariant
                    opacity: parent.pressed ? 0.4 : parent.hovered ? 0.3 : 0.5
                }

                contentItem: Text {
                    anchors.centerIn: parent
                    text: parent.text
                    color: ThemeManager.onSurface
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                }

                onClicked: fileDialog.open()
            }
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
                    text: qsTr("Status")
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    color: ThemeManager.surfaceVariant
                    radius: 6
                    border.color: ThemeManager.onSurface
                    border.width: 1
                    opacity: 0.5

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: recorder.streamingState
                        font.pixelSize: 14
                        font.family: "IBM Plex Sans"
                        color: statusColor
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: recorder.streamingBitrate
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: ThemeManager.onSurface
                        opacity: 0.6
                    }
                }
            }

            Button {
                Layout.preferredWidth: 140
                Layout.preferredHeight: 40
                text: recorder.isStreaming ? qsTr("Cancel") : qsTr("Start Streaming")
                font.pixelSize: 13
                font.family: "IBM Plex Sans"
                enabled: urlField.text !== "" && sourceField.text !== ""

                background: Rectangle {
                    radius: 6
                    color: parent.pressed ? ThemeManager.accentColor
                           : parent.hovered ? ThemeManager.accentColor
                           : ThemeManager.primary
                    opacity: parent.pressed ? 0.8 : parent.hovered ? 0.7 : 1.0
                }

                contentItem: Text {
                    anchors.centerIn: parent
                    text: parent.text
                    color: "#ffffff"
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    font.bold: true
                }

                onClicked: {
                    if (recorder.isStreaming) {
                        recorder.cancelStreaming()
                    } else {
                        recorder.startStreaming(urlField.text, sourceField.text)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    text: qsTr("Progress")
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.7
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: Math.round(recorder.streamingProgress * 100) + "%"
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    opacity: 0.6
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 8
                color: ThemeManager.surfaceVariant
                radius: 4

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * (recorder.isStreaming ? recorder.streamingProgress : 0)
                    color: ThemeManager.accentColor
                    radius: 4

                    Behavior on width {
                        NumberAnimation {
                            duration: 100
                            easing.type: Easing.Linear
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: ThemeManager.surfaceVariant
                radius: 8
                opacity: 0.3

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: statusMessage
                        font.pixelSize: 14
                        font.family: "IBM Plex Sans"
                        color: ThemeManager.onSurface
                        opacity: 0.5
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select Source File")
        nameFilters: [
            "MP4 files (*.mp4)",
            "All files (*)"
        ]
        onAccepted: {
            var filePath = selectedFile.toString()
            if (filePath.startsWith("file:///")) {
                filePath = decodeURIComponent(filePath.substring(8))
            }
            sourceField.text = filePath
        }
    }

    property string protocolLabel: "RTMP"
    property string protocolColor: ThemeManager.onSurface

    function updateProtocolDetection() {
        var url = urlField.text.toLowerCase()
        if (url.startsWith("rtmp://")) {
            protocolLabel = "RTMP"
            protocolColor = ThemeManager.onSurface
        } else if (url.startsWith("srt://")) {
            protocolLabel = "SRT"
            protocolColor = ThemeManager.accentColor
        } else {
            protocolLabel = "Unknown"
            protocolColor = ThemeManager.onSurface
            opacity = 0.4
        }
    }

    property string statusColor: ThemeManager.onSurface

    function updateStatusColor() {
        var state = recorder.streamingState
        if (state === "Streaming") {
            statusColor = ThemeManager.accentColor
        } else if (state === "Failed") {
            statusColor = ThemeManager.errorColor
        } else if (state === "Completed") {
            statusColor = ThemeManager.primary
        } else if (state === "Cancelled") {
            statusColor = ThemeManager.onSurface
        } else {
            statusColor = ThemeManager.onSurface
        }
    }

    property string statusMessage: qsTr("Configure streaming settings above")

    function updateStatusMessage() {
        var state = recorder.streamingState
        if (state === "Idle") {
            statusMessage = qsTr("Configure streaming settings above")
        } else if (state === "Connecting") {
            statusMessage = qsTr("Connecting to stream server...")
        } else if (state === "Streaming") {
            statusMessage = qsTr("Streaming in progress...")
        } else if (state === "Completed") {
            statusMessage = qsTr("Streaming completed successfully!")
        } else if (state === "Failed") {
            statusMessage = qsTr("Streaming failed. Check URL and network.")
        } else if (state === "Cancelled") {
            statusMessage = qsTr("Streaming cancelled by user.")
        }
    }

    Connections {
        target: recorder
        function onStreamingStateChanged() {
            updateStatusColor()
            updateStatusMessage()
        }
    }

    Component.onCompleted: {
        updateProtocolDetection()
        updateStatusColor()
        updateStatusMessage()
    }
}
