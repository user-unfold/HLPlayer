import QtQuick
import QtQuick.Controls
import HLPlayer

Item {
    id: root

    property real position: 0
    property real duration: 1
    property real bufferedPosition: 0
    property bool seekEnabled: true
    property real pendingSeekValue: -1
    property bool _seekDragging: false

    signal seekRequested(real targetSeconds)
    signal hoverTimeChanged(real timeSeconds)

    implicitHeight: 24

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left; anchors.leftMargin: 8
        anchors.right: parent.right; anchors.rightMargin: 8
        height: hov.containsMouse || seekSlider.pressed ? 8 : 4
        radius: height / 2
        color: Qt.rgba(1, 1, 1, 0.12)
        Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * (root.duration > 0 ? root.bufferedPosition / root.duration : 0)
            height: parent.height
            radius: height / 2
            color: Qt.rgba(139/255, 92/255, 246/255, 0.3)
        }

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * (root.duration > 0
                    ? (root._seekDragging ? seekSlider.value / root.duration : root.position / root.duration)
                    : 0)
            height: parent.height
            radius: height / 2
            color: "#8B5CF6"
        }

        Rectangle {
            id: handle
            anchors.verticalCenter: parent.verticalCenter
            x: (root.duration > 0 ? (root._seekDragging ? seekSlider.value / root.duration : root.position / root.duration) : 0)
               * track.width - width / 2
            width: (hov.containsMouse || seekSlider.pressed) ? 16 : 0
            height: width; radius: width / 2
            color: "#FFFFFF"
            border.color: "#8B5CF6"; border.width: 2
            opacity: (hov.containsMouse || seekSlider.pressed) ? 1.0 : 0.0
            Behavior on width { NumberAnimation { duration: 100 } }
            Behavior on opacity { NumberAnimation { duration: 100 } }
        }
    }

    Slider {
        id: seekSlider
        anchors.fill: parent
        from: 0; to: root.duration > 0 ? root.duration : 1
        value: root._seekDragging ? value : root.position
        opacity: 0.0

        onMoved: { if (root.seekEnabled) root.pendingSeekValue = value }
        onPressedChanged: {
            if (pressed) { root._seekDragging = true; return }
            if (!root.seekEnabled) { root.pendingSeekValue = -1; root._seekDragging = false; return }
            var target = root.pendingSeekValue >= 0 ? root.pendingSeekValue : valueAt(position)
            root.pendingSeekValue = -1
            root.seekRequested(target)
            root._seekDragging = false
        }
    }

    MouseArea {
        id: hov
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton

        property real hoverTime: {
            if (!containsMouse || root.duration <= 0) return -1
            var frac = (mouseX - 8) / track.width
            frac = Math.max(0, Math.min(1, frac))
            return frac * root.duration
        }
        onPositionChanged: { if (hoverTime >= 0) root.hoverTimeChanged(hoverTime) }
        onExited: root.hoverTimeChanged(-1)
    }
}
