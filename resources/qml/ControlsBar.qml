import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

    Rectangle {
        id: root

        height: 48
        color: ThemeManager.surfaceVariant
        opacity: 0.9

        property var player: null
        property real pendingSeekValue: -1

        Behavior on opacity { NumberAnimation { duration: 150 } }

        RowLayout {
            id: layout
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            Button {
                id: playPauseBtn
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                flat: true

                Accessible.role: Accessible.Button
                Accessible.name: root.player && root.player.isPlaying
                                 ? PlayerI18nContext.tr("Pause")
                                 : PlayerI18nContext.tr("Play")

                background: Rectangle {
                    radius: 4
                    color: playPauseBtn.hovered ? ThemeManager.surfaceVariant : "transparent"
                }

                contentItem: Text {
                    anchors.centerIn: parent
                    text: root.player && root.player.isPlaying ? "\u23F8" : "\u25B6"
                    font.pixelSize: 18
                    color: ThemeManager.onSurface
                }

                onClicked: function() {
                    if (root.player) {
                        if (root.player.isPlaying) {
                            root.player.pause()
                        } else {
                            root.player.play()
                        }
                    }
                }
            }

            Slider {
                id: seekSlider
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                from: 0.0
                to: root.player ? Math.max(root.player.duration, 1.0) : 1.0

                // ── Seek state isolation ──────────────────────────
                property bool _seekDragging: false

                Binding on value {
                    when: !seekSlider._seekDragging
                    value: root.player ? root.player.position : 0.0
                }

                Behavior on value {
                    enabled: !seekSlider._seekDragging
                    NumberAnimation {
                        duration: 100
                        easing.type: Easing.OutCubic
                    }
                }

                Accessible.role: Accessible.Slider
                Accessible.name: PlayerI18nContext.tr("Seek")

                background: Rectangle {
                    x: seekSlider.leftPadding
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: seekSlider.availableWidth
                    height: 4
                    radius: 2
                    color: ThemeManager.surfaceVariant

                    Rectangle {
                        width: seekSlider.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: ThemeManager.primary
                    }
                }

                handle: Rectangle {
                    x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                    y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                    width: 14
                    height: 14
                    radius: 7
                    color: ThemeManager.primary
                    border.color: ThemeManager.onSurface
                    border.width: 1
                }

                onMoved: function() {
                    if (root.player) {
                        pendingSeekValue = seekSlider.value
                    }
                }

                onPressedChanged: {
                    if (seekSlider.pressed) {
                        _seekDragging = true
                        return
                    }
                    // ── Released ──
                    if (!root.player) {
                        pendingSeekValue = -1
                        _seekDragging = false
                        return
                    }
                    var target = pendingSeekValue >= 0 ? pendingSeekValue : valueAt(position)
                    pendingSeekValue = -1
                    root.player.seek(target)
                    _seekDragging = false
                }
            }

        Text {
            id: timeLabel
            Layout.preferredWidth: 80
            horizontalAlignment: Text.AlignRight
            font.pixelSize: 12
            color: ThemeManager.onSurface

            function formatTime(seconds) {
                var m = Math.floor(seconds / 60)
                var s = Math.floor(seconds % 60)
                return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
            }

            text: formatTime(root.player ? root.player.position : 0)
                  + " / "
                  + formatTime(root.player ? root.player.duration : 0)

            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Slider {
            id: volumeSlider
            Layout.preferredWidth: 80
            Layout.alignment: Qt.AlignVCenter
            from: 0.0
            to: 1.0
            value: root.player ? root.player.volume : 1.0

            Accessible.role: Accessible.Slider
            Accessible.name: PlayerI18nContext.tr("Volume")

            background: Rectangle {
                x: volumeSlider.leftPadding
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: volumeSlider.availableWidth
                height: 4
                radius: 2
                color: ThemeManager.surfaceVariant

                Rectangle {
                    width: volumeSlider.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: ThemeManager.primary
                }
            }

            handle: Rectangle {
                x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: 12
                height: 12
                radius: 6
                color: ThemeManager.primary
            }

            onMoved: function() {
                if (root.player) {
                    root.player.setVolume(volumeSlider.value)
                }
            }
        }

        Button {
            id: fullscreenBtn
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            flat: true

            Accessible.role: Accessible.Button
            Accessible.name: PlayerI18nContext.tr("Fullscreen")

            background: Rectangle {
                radius: 4
                color: fullscreenBtn.hovered ? ThemeManager.surfaceVariant : "transparent"
            }

            contentItem: Text {
                anchors.centerIn: parent
                text: "\u26F6"
                font.pixelSize: 16
                color: ThemeManager.onSurface
            }
        }
    }
}
