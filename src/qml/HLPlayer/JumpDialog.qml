import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

Popup {
    id: root
    modal: true
    width: 320; height: 140
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    padding: 20

    property var player: null
    property var formatTimeFunc: null
    signal jumpRequested(real targetSeconds)

    background: Rectangle {
        color: ThemeManager.bgSidebar; radius: ThemeManager.isDarkMode ? 10 : 6
        border.color: ThemeManager.border; border.width: 0.5
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 16

        Text {
            text: qsTr("Jump to Time")
            font.pixelSize: 15; font.bold: true
            color: ThemeManager.textPrimary
        }

        TextField {
            id: timeInput
            Layout.fillWidth: true
            placeholderText: qsTr("Enter time (e.g. 90 or 1:30)")
            font.pixelSize: 14
            color: ThemeManager.textPrimary
            selectByMouse: true

            background: Rectangle {
                radius: 6
                color: Qt.rgba(1, 1, 1, 0.06)
                border.color: timeInput.activeFocus ? ThemeManager.accentColor : ThemeManager.border
                border.width: 1
            }

            onTextChanged: {
                var target = parseInput(text)
                if (target >= 0 && root.formatTimeFunc)
                    previewText.text = root.formatTimeFunc(target)
                else
                    previewText.text = ""
            }

            onAccepted: attemptJump()

            function parseInput(t) {
                if (!t) return -1
                if (t.includes(":")) {
                    var parts = t.split(":")
                    if (parts.length === 2)
                        return parseInt(parts[0]) * 60 + parseInt(parts[1])
                    if (parts.length === 3)
                        return parseInt(parts[0]) * 3600 + parseInt(parts[1]) * 60 + parseInt(parts[2])
                    return -1
                }
                var secs = parseFloat(t)
                return isNaN(secs) ? -1 : secs
            }
        }

        Text {
            id: previewText
            font.pixelSize: 13
            color: ThemeManager.textSecondary
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }

            Button {
                text: qsTr("Cancel")
                flat: true
                font.pixelSize: 13
                onClicked: root.close()
                background: Rectangle {
                    radius: 6; color: parent.hovered ? ThemeManager.hoverHighlight : "transparent"
                }
                contentItem: Text {
                    text: parent.text; font: parent.font
                    color: ThemeManager.textPrimary
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: qsTr("Go")
                flat: true
                font.pixelSize: 13; font.bold: true
                onClicked: attemptJump()
                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? ThemeManager.accentColor : ThemeManager.accentColor
                    opacity: parent.hovered ? 0.8 : 0.6
                }
                contentItem: Text {
                    text: parent.text; font: parent.font
                    color: ThemeManager.textPrimary
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    function attemptJump() {
        var target = timeInput.parseInput(timeInput.text)
        if (target < 0) return
        if (root.player && target > root.player.duration)
            target = root.player.duration
        root.jumpRequested(target)
        root.close()
    }

    onOpened: { timeInput.text = ""; timeInput.forceActiveFocus() }
}
