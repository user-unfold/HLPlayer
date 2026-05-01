import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

Rectangle {
    id: root

    property string windowTitle: ""
    property bool isFullScreen: false

    signal minimizeClicked()
    signal maximizeClicked()
    signal closeClicked()

    height: 48
    color: "transparent"
    z: 100

    gradient: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.85) }
        GradientStop { position: 1.0; color: "transparent" }
    }

    MouseArea {
        anchors.fill: parent
        property point lastMousePos: Qt.point(0, 0)
        onPressed: function(mouse) { lastMousePos = Qt.point(mouse.x, mouse.y) }
        onPositionChanged: function(mouse) {
            if (pressed && root.parent && root.parent.parent) {
                var dx = mouse.x - lastMousePos.x
                var dy = mouse.y - lastMousePos.y
                root.parent.parent.x += dx
                root.parent.parent.y += dy
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 0

        Rectangle {
            width: 28; height: 28
            radius: 14
            color: "#8B5CF6"
            Layout.alignment: Qt.AlignVCenter

            Text {
                anchors.centerIn: parent
                text: "HL"
                font.pixelSize: 13; font.bold: true
                color: "#FFFFFF"
            }
        }

        Item { width: 8; height: 1 }

        Text {
            text: "HLPlayer"
            font.pixelSize: 14; font.bold: true
            font.family: "IBM Plex Sans"
            color: ThemeManager.textPrimary
            Layout.alignment: Qt.AlignVCenter
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: true }

        Text {
            width: Math.min(implicitWidth, parent ? parent.width - 200 : 400)
            text: root.windowTitle
            font.pixelSize: 13
            font.family: "IBM Plex Sans"
            color: "#888888"
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true

            ToolTip {
                visible: parent.truncated && titleHoverArea.containsMouse
                text: root.windowTitle
                delay: 500
            }

            MouseArea {
                id: titleHoverArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
        }

        Item { Layout.fillWidth: true; Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            IconButton {
                iconText: "\u2500"
                iconSize: 14; buttonSize: 32
                onClicked: root.minimizeClicked()
            }

            IconButton {
                iconText: root.isFullScreen ? "\u29C9" : "\u25A1"
                iconSize: 14; buttonSize: 32
                onClicked: root.maximizeClicked()
            }

            IconButton {
                id: closeBtn
                iconText: "\u2715"
                iconSize: 14; buttonSize: 32
                customColor: closeBtn.hovered ? "#FFFFFF" : ThemeManager.textPrimary

                background: Rectangle {
                    radius: closeBtn.radius
                    color: closeBtn.hovered ? "#EF4444" : "transparent"
                    opacity: closeBtn.hovered ? 1.0 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 150 } }
                }

                onClicked: root.closeClicked()
            }
        }
    }
}
