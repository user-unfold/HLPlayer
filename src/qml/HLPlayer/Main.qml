import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

ApplicationWindow {
    id: root
    width: 1280
    height: 720
    visible: true
    title: currentTitle ? "HLPlayer - " + currentTitle : "HLPlayer"
    color: ThemeManager.bgMain
    flags: Qt.FramelessWindowHint | Qt.Window

    property string currentTitle: ""
    property bool controlsVisible: true
    property real playbackSpeed: player.playbackRate
    property bool playlistVisible: false
    property real previousVolume: 1.0
    property bool isMuted: player.volume === 0
    property real pendingSeekValue: -1
    property bool streamOsdVisible: true
    property bool seeking: false

    function isNetworkSource() {
        if (!player.source || player.source === "") return false
        var protocols = ["rtsp://", "rtmp://", "rtmps://", "http://", "https://", "udp://", "rtp://"]
        return protocols.some(function(p) { return player.source.startsWith(p) })
    }

    readonly property int space1: 8
    readonly property int space2: 16
    readonly property int space3: 24

    onPlaybackSpeedChanged: {
        osdOverlay.message = playbackSpeed.toFixed(1) + "\u00D7"
        osdOverlay.iconText = ""
        osdOverlay.show()
    }

    Timer {
        id: hideControlsTimer
        interval: 3000
        repeat: false
        onTriggered: root.controlsVisible = false
    }

    Timer {
        id: seekTimeout
        interval: 2000
        repeat: false
        onTriggered: root.seeking = false
    }

    function resetHideTimer() {
        root.controlsVisible = true
        hideControlsTimer.restart()
    }

    MouseArea {
        id: hoverTracker
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: resetHideTimer()
    }

    Shortcut { sequence: "Escape"; enabled: root.visibility === Window.FullScreen; onActivated: { root.showNormal(); root.controlsVisible = true; hideControlsTimer.stop() } }
    Shortcut { sequence: "Space"; onActivated: togglePlayPause() }
    Shortcut { sequence: "Ctrl+U"; onActivated: urlDialog.open() }
    Shortcut { sequence: "Ctrl+O"; onActivated: fileDialog.open() }
    Shortcut { sequence: "Left"; onActivated: { if (canSeek()) { player.seek(Math.max(0, player.position - 10)); showOsd("\u25C0\u25C0 -10s") } } }
    Shortcut { sequence: "Right"; onActivated: { if (canSeek()) { player.seek(Math.min(player.duration, player.position + 10)); showOsd("\u25B6\u25B6 +10s") } } }
    Shortcut { sequence: "Up"; onActivated: { player.volume = Math.min(1.0, player.volume + 0.05); showOsdVol() } }
    Shortcut { sequence: "Down"; onActivated: { player.volume = Math.max(0.0, player.volume - 0.05); showOsdVol() } }
    Shortcut { sequence: "F"; onActivated: toggleFullScreen() }
    Shortcut { sequence: "M"; onActivated: toggleMute() }
    Shortcut { sequence: "N"; onActivated: playNext() }
    Shortcut { sequence: "P"; onActivated: playPrev() }
    Shortcut { sequence: "L"; onActivated: root.playlistVisible = !root.playlistVisible }
    Shortcut { sequence: "I"; onActivated: videoInfoHud.visible = !videoInfoHud.visible }
    Shortcut { sequence: "G"; onActivated: jumpDialog.open() }
    Shortcut { sequence: "Delete"; onActivated: { if (playlistView.currentIndex >= 0) playlist.remove(playlistView.currentIndex) } }

    function hasPlayableSource() { return player.source && player.source !== "" }
    function canSeek() { return hasPlayableSource() && player.duration > 0 && !seeking }

    function togglePlayPause() {
        if (!hasPlayableSource()) { showOsd("No media loaded"); return }
        if (player.isPlaying) { player.pause(); showOsdDisplay("\u23F8", "Paused") }
        else { player.play(); showOsdDisplay("\u25B6", "Playing") }
    }
    function toggleFullScreen() {
        if (root.visibility === Window.FullScreen) {
            root.showNormal()
            root.controlsVisible = true
            hideControlsTimer.stop()
        } else {
            root.playlistVisible = false
            root.showFullScreen()
            hideControlsTimer.restart()
        }
    }
    function toggleMute() {
        if (player.volume > 0) { previousVolume = player.volume; player.volume = 0; showOsd("\uD83D\uDD07 Muted") }
        else { player.volume = previousVolume > 0 ? previousVolume : 1.0; showOsd("\uD83D\uDD0A Unmuted") }
    }
    function playNext() {
        if (playlist.count === 0) return
        var next = playlist.currentIndex + 1
        if (next >= playlist.count) next = 0
        playlist.currentIndex = next
        loadAndPlay(next)
    }
    function playPrev() {
        if (playlist.count === 0) return
        var prev = playlist.currentIndex - 1
        if (prev < 0) prev = playlist.count - 1
        playlist.currentIndex = prev
        loadAndPlay(prev)
    }

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0) return "00:00"
        var h = Math.floor(seconds / 3600)
        var m = Math.floor((seconds % 3600) / 60)
        var s = Math.floor(seconds % 60)
        var res = ""
        if (h > 0) res += (h < 10 ? "0" : "") + h + ":"
        res += (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
        return res
    }

    function showOsd(text) { showOsdDisplay("", text) }
    function showOsdVol() { showOsdDisplay("\uD83D\uDD0A", "Vol: " + Math.round(player.volume * 100) + "%") }

    function showOsdDisplay(icon, msg) {
        osdOverlay.iconText = icon
        osdOverlay.message = msg
        osdOverlay.show()
    }

    function showToast(msg) {
        toastText.text = msg
        toastAnimation.stop()
        toastRect.opacity = 0.9
        toastAnimation.start()
    }

    Component.onCompleted: { ThemeManager.theme = ThemeManager.Dark }
    Component.onDestruction: { if (recorder.recording) recorder.stop() }

    PlaylistModel { id: playlist }
    StreamRecorderBridge { id: recorder }

    QMLPlayer {
        id: player
        onVideoResolutionChanged: {
            if (player.videoWidth > 0 && player.videoHeight > 0) {
                var w = player.videoWidth
                var h = player.videoHeight
                var maxW = Screen.desktopAvailableWidth * 0.8
                var maxH = Screen.desktopAvailableHeight * 0.75
                if (w > maxW) { var s = maxW / w; w = Math.round(w * s); h = Math.round(h * s) }
                if (h > maxH) { var s2 = maxH / h; w = Math.round(w * s2); h = Math.round(h * s2) }
                videoArea.implicitWidth = Math.max(w, 320)
                videoArea.implicitHeight = Math.max(h, 240)
            }
        }
        onSourceChanged: {
            if (source) {
                var path = source
                if (path.startsWith("file:///")) path = decodeURIComponent(path.substring(8).replace(/\\/g, "/"))
                root.currentTitle = path.split("/").pop()
            }
        }
        onErrorChanged: { if (error !== "") showToast(error) }
        onStateChanged: {
            if (player.state === 4 || player.state === 5) {
                root.seeking = false
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: PlayerI18nContext.tr("Open Media File")
        nameFilters: [
            "Common Media (*.mp4 *.mkv *.avi *.mov *.webm *.mp3 *.flac *.wav)",
            "All Supported (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.ts *.m2ts *.mts *.3gp *.rmvb *.asf *.ogm *.m4v *.divx *.mpg *.mpeg *.mp3 *.flac *.wav *.ogg *.aac *.m4a *.opus *.wma *.ape *.tak *.mka *.srt *.ass *.ssa *.sub *.idx *.sup *.vtt *.m3u *.m3u8 *.pls *.mpd)",
            "Video (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.ts *.m2ts *.mts *.3gp *.rmvb *.asf *.ogm *.m4v *.divx *.mpg *.mpeg)",
            "Audio (*.mp3 *.flac *.wav *.ogg *.aac *.m4a *.opus *.wma *.ape *.tak *.mka)",
            "All files (*)"
        ]
        onAccepted: {
            var urls = []
            for (var i = 0; i < selectedFiles.length; i++) {
                var raw = selectedFiles[i]
                var localPath = raw && raw.toLocalFile ? raw.toLocalFile() : (raw && raw.toString ? raw.toString() : String(raw))
                if (localPath !== "") urls.push(localPath)
            }
            if (urls.length === 0) return
            playlist.addFiles(urls)
            playlist.currentIndex = playlist.count - urls.length
            loadAndPlay(playlist.currentIndex)
        }
    }

    OpenUrlDialog {
        id: urlDialog
        playlist: playlist
        loadAndPlayFunction: loadAndPlay
    }

    function loadAndPlay(index) {
        var url = playlist.getUrl(index)
        if (!url) return
        var protocols = ["rtsp://", "rtmp://", "rtmps://", "http://", "https://", "udp://", "rtp://"]
        var isNet = protocols.some(function(p) { return url.startsWith(p) })
        var finalUrl = url
        if (!isNet && !url.startsWith("file://")) finalUrl = "file:///" + encodeURI(url.replace(/\\/g, "/"))
        player.source = finalUrl
        player.play()
    }

    TitleBar {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        windowTitle: root.currentTitle || "HLPlayer"
        isFullScreen: root.visibility === Window.FullScreen
        visible: root.visibility !== Window.FullScreen

        onMinimizeClicked: root.showMinimized()
        onMaximizeClicked: {
            if (root.visibility === Window.Maximized) root.showNormal()
            else root.showMaximized()
        }
        onCloseClicked: root.close()
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: root.visibility === Window.FullScreen ? 0 : titleBar.height
        spacing: 0

        Rectangle {
            id: videoContainer
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#000000"

            Item {
                id: videoArea
                anchors.fill: parent

                VideoOutputItem {
                    id: videoOutput
                    anchors.fill: parent
                    clip: true
                    videoSink: player.videoSink
                }

                DropArea {
                    id: dropArea
                    anchors.fill: parent
                    onDropped: function(drop) {
                        if (drop.hasUrls) {
                            var urls = []
                            for (var i = 0; i < drop.urls.length; i++) urls.push(drop.urls[i])
                            playlist.addFiles(urls)
                            playlist.currentIndex = playlist.count - urls.length
                            loadAndPlay(playlist.currentIndex)
                        }
                    }
                }

                MouseArea {
                    id: videoMouseArea
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    hoverEnabled: true
                    onDoubleClicked: { singleClickTimer.stop(); toggleFullScreen() }
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) contextMenu.popup()
                        else if (mouse.button === Qt.MiddleButton) togglePlayPause()
                        else singleClickTimer.start()
                    }
                    onWheel: function(wheel) {
                        var vDelta = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                        player.volume = Math.max(0.0, Math.min(1.0, player.volume + vDelta))
                        showOsdVol()
                    }
                }

                Timer {
                    id: singleClickTimer
                    interval: 250
                    repeat: false
                    onTriggered: togglePlayPause()
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 16
                    visible: player.state === 0 && !root.currentTitle

                    Rectangle {
                        width: 400; height: 220
                        radius: 16
                        color: "transparent"
                        border.color: dropArea.containsDrag ? "#8B5CF6" : Qt.rgba(1, 1, 1, 0.08)
                        border.width: 2
                        anchors.horizontalCenter: parent.horizontalCenter

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: 14
                            color: "transparent"
                            border.color: dropArea.containsDrag ? "#A78BFA" : Qt.rgba(1, 1, 1, 0.04)
                            border.width: 1
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 12

                            Text {
                                text: "\u25B6"
                                font.pixelSize: 48
                                color: dropArea.containsDrag ? "#8B5CF6" : "#444444"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }

                            Text {
                                text: dropArea.containsDrag
                                      ? "\uD83D\uDCE5 松开以打开"
                                      : PlayerI18nContext.tr("\u62D6\u5165\u6587\u4EF6\u6216\u6309 Ctrl+O \u6253\u5F00")
                                font.pixelSize: 16
                                font.family: "IBM Plex Sans"
                                color: dropArea.containsDrag ? "#8B5CF6" : "#666666"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }

                            Text {
                                text: PlayerI18nContext.tr("\u652F\u6301 MP4 / MKV / AVI / RTSP \u7B49\u683C\u5F0F")
                                font.pixelSize: 12
                                font.family: "IBM Plex Sans"
                                color: "#444444"
                                anchors.horizontalCenter: parent.horizontalCenter
                            }

                            Button {
                                id: openFileBtn
                                anchors.horizontalCenter: parent.horizontalCenter
                                implicitWidth: 140; implicitHeight: 40
                                flat: true

                                background: Rectangle {
                                    radius: 8
                                    color: openFileBtn.hovered ? "#A78BFA" : "#8B5CF6"
                                    Behavior on color { ColorAnimation { duration: 100 } }
                                }

                                contentItem: Text {
                                    text: PlayerI18nContext.tr("\u6253\u5F00\u6587\u4EF6")
                                    font.pixelSize: 14; font.bold: true
                                    color: "#FFFFFF"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                onClicked: fileDialog.open()

                                transform: Translate {
                                    y: openFileBtn.hovered ? -2 : 0
                                    Behavior on y { NumberAnimation { duration: 100 } }
                                }
                            }
                        }
                    }
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    width: 48; height: 48
                    visible: player.state === 1 || player.state === 3
                }

                StreamOSD {
                    player: player
                    visible: root.streamOsdVisible && isNetworkSource()
                }

                Rectangle {
                    id: osdOverlay
                    anchors.centerIn: parent
                    width: osdRow.implicitWidth + 32
                    height: osdRow.implicitHeight + 20
                    color: Qt.rgba(0, 0, 0, 0.75)
                    radius: 10
                    opacity: 0; visible: opacity > 0
                    z: 50

                    property string iconText: ""
                    property string message: ""

                    Row {
                        id: osdRow
                        anchors.centerIn: parent
                        spacing: 10
                        Text {
                            text: osdOverlay.iconText
                            font.pixelSize: 22; color: "#ffffff"
                            visible: osdOverlay.iconText !== ""
                        }
                        Text {
                            text: osdOverlay.message
                            font.pixelSize: 16; font.bold: true; color: "#ffffff"
                        }
                    }

                    function show() {
                        opacity = 0.85
                        osdFadeOut.stop(); osdDismissTimer.restart()
                    }

                    NumberAnimation {
                        id: osdFadeOut
                        target: osdOverlay; property: "opacity"; to: 0; duration: 400
                        easing.type: Easing.InCubic
                    }

                    Timer {
                        id: osdDismissTimer
                        interval: 1500
                        onTriggered: osdFadeOut.start()
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    visible: recorder.recording
                    radius: 4; color: Qt.rgba(0, 0, 0, 0.8); height: 28; width: recRow.implicitWidth + 20

                    RowLayout {
                        id: recRow
                        anchors.centerIn: parent
                        spacing: 8

                        Rectangle {
                            width: 10; height: 10; radius: 5; color: "#ff0000"
                            SequentialAnimation on opacity {
                                running: recorder.recording; loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 500 }
                                NumberAnimation { to: 1.0; duration: 500 }
                            }
                        }
                        Text { text: "REC"; font.pixelSize: 12; font.bold: true; color: "#ffffff"; font.family: "IBM Plex Sans" }
                        Text { text: formatTime(recorder.duration); font.pixelSize: 12; color: "#ffffff"; font.family: "IBM Plex Sans" }
                    }
                }

                VideoInfoHUD {
                    id: videoInfoHud
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 16
                    visible: false
                    player: player
                    formatTimeFunc: formatTime
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 3
                    color: Qt.rgba(1, 1, 1, 0.15)
                    visible: !root.controlsVisible && player.duration > 0
                    z: 10

                    Rectangle {
                        width: parent.width * (player.duration > 0 ? player.position / player.duration : 0)
                        height: parent.height
                        color: ThemeManager.accentColor
                    }
                }
            }

            Rectangle {
                id: controlsOverlay
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: root.controlsVisible ? 100 : 0
                opacity: root.controlsVisible ? 1.0 : 0.0
                Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 200 } }

                gradient: Gradient {
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.4; color: Qt.rgba(0, 0, 0, 0.75) }
                    GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.95) }
                }

                ColumnLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.bottomMargin: 6
                    spacing: 0

                    ProgressBar {
                        id: progressBar
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        position: player.position
                        duration: player.duration
                        seekEnabled: canSeek()

                        onSeekRequested: function(target) {
                            if (root.seeking) return
                            root.seeking = true
                            player.seek(target)
                            showOsd("Seeking...")
                            seekTimeout.restart()
                        }
                        onHoverTimeChanged: function(t) {
                            hoverTooltip.visible = t >= 0
                            if (t >= 0) {
                                hoverTimeText.text = formatTime(t)
                                var frac = t / Math.max(player.duration, 1)
                                var pt = progressBar.mapToItem(controlsOverlay, frac * progressBar.width, 0)
                                hoverTooltip.x = Math.max(0, Math.min(pt.x - hoverTooltip.width / 2,
                                    controlsOverlay.width - hoverTooltip.width))
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 4

                        IconButton {
                            iconText: "\u23EE"
                            iconSize: 16; buttonSize: 34
                            customColor: "#CCCCCC"
                            onClicked: playPrev()
                        }

                        IconButton {
                            id: playPauseBtn
                            iconText: player.isPlaying ? "\u23F8" : "\u25B6"
                            iconSize: 20; buttonSize: 44
                            isAccent: true
                            onClicked: togglePlayPause()

                            scale: 1.0
                            Behavior on scale { NumberAnimation { duration: 100 } }
                            Connections {
                                target: player
                                function onIsPlayingChanged() { playPauseBtn.scale = 0.8; playPauseBtn.scale = 1.0 }
                            }
                        }

                        IconButton {
                            iconText: "\u23ED"
                            iconSize: 16; buttonSize: 34
                            customColor: "#CCCCCC"
                            onClicked: playNext()
                        }

                        IconButton {
                            iconText: "\u25A0"
                            iconSize: 12; buttonSize: 34
                            customColor: "#CCCCCC"
                            onClicked: { player.stop(); showOsd("Stopped") }
                        }

                        Item { width: 8; height: 1 }

                        IconButton {
                            id: muteBtn
                            iconText: {
                                if (player.volume === 0) return "\uD83D\uDD07"
                                if (player.volume < 0.3) return "\uD83D\uDD08"
                                if (player.volume < 0.7) return "\uD83D\uDD09"
                                return "\uD83D\uDD0A"
                            }
                            iconSize: 16; buttonSize: 34
                            customColor: "#CCCCCC"
                            onClicked: toggleMute()
                        }

                        Slider {
                            id: volumeSlider
                            Layout.preferredWidth: 80
                            implicitHeight: 24
                            from: 0; to: 1
                            value: player.volume

                            background: Rectangle {
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                width: volumeSlider.availableWidth; height: 4; radius: 2
                                color: Qt.rgba(1, 1, 1, 0.12)
                                Rectangle {
                                    width: volumeSlider.visualPosition * parent.width
                                    height: parent.height; radius: 2
                                    color: "#8B5CF6"
                                }
                            }
                            handle: Rectangle {
                                x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                width: 12; height: 12; radius: 6
                                color: "#FFFFFF"; border.color: "#8B5CF6"; border.width: 1
                            }
                            onMoved: { player.volume = value; if (value > 0) previousVolume = value }
                        }

                        Text {
                            text: formatTime(player.position)
                            font.pixelSize: 12; font.family: "IBM Plex Sans"
                            color: "#FFFFFF"
                        }
                        Text {
                            text: " / "
                            font.pixelSize: 12; font.family: "IBM Plex Sans"
                            color: "#888888"
                        }
                        Text {
                            text: formatTime(player.duration)
                            font.pixelSize: 12; font.family: "IBM Plex Sans"
                            color: "#888888"
                        }

                        Item { width: 8; height: 1 }
                        Item { Layout.fillWidth: true }

                        Button {
                            id: speedBtn
                            flat: true
                            Layout.preferredWidth: 48; Layout.preferredHeight: 30

                            ToolTip {
                                visible: speedHover.containsMouse
                                text: "\u901F\u5EA6 (" + player.playbackRate.toFixed(1) + "\u00D7)"
                                delay: 500
                            }

                            MouseArea {
                                id: speedHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            background: Rectangle {
                                radius: 6
                                color: speedBtn.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                                Behavior on color { ColorAnimation { duration: 150 } }
                            }
                            contentItem: Text {
                                text: player.playbackRate.toFixed(1) + "\u00D7"
                                font.pixelSize: 12; font.family: "IBM Plex Sans"
                                color: Math.abs(player.playbackRate - 1.0) > 0.01 ? "#8B5CF6" : "#CCCCCC"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: speedPopup.open()
                        }

                        Popup {
                            id: speedPopup
                            x: speedBtn.x + speedBtn.width / 2 - width / 2
                            y: -height - 8
                            width: 80; padding: 6
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                            background: Rectangle {
                                color: "#1E1E1E"; radius: 10
                                border.color: "#2A2A2A"; border.width: 0.5
                            }

                            contentItem: Column {
                                spacing: 2
                                Repeater {
                                    model: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                                    delegate: Rectangle {
                                        width: 68; height: 30; radius: 6
                                        color: {
                                            if (Math.abs(player.playbackRate - modelData) < 0.005) return "#8B5CF6"
                                            if (siMa.containsMouse) return Qt.rgba(1, 1, 1, 0.08)
                                            return "transparent"
                                        }
                                        opacity: Math.abs(player.playbackRate - modelData) < 0.005 ? 0.2 : 1.0

                                        Text {
                                            anchors.centerIn: parent
                                            text: modelData.toFixed(1) + "\u00D7"
                                            font.pixelSize: 12
                                            color: Math.abs(player.playbackRate - modelData) < 0.005 ? "#8B5CF6" : "#CCCCCC"
                                            font.bold: Math.abs(player.playbackRate - modelData) < 0.005
                                        }

                                        MouseArea {
                                            id: siMa
                                            anchors.fill: parent; hoverEnabled: true
                                            onClicked: { player.playbackRate = modelData; speedPopup.close() }
        }
    }
                                }
                            }
                        }

                        IconButton {
                            iconText: "CC"
                            iconSize: 11; buttonSize: 34
                            customColor: player.hasSubtitles && player.subtitleVisible ? "#8B5CF6" : "#CCCCCC"

                            ToolTip {
                                visible: subHover.containsMouse
                                text: "\u5B57\u5E55 (Ctrl+S)"
                                delay: 500
                            }

                            MouseArea {
                                id: subHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            onClicked: {
                                if (player.hasSubtitles) {
                                    player.toggleSubtitles()
                                    showOsd(player.subtitleVisible ? "Subtitles: ON" : "Subtitles: OFF")
                                }
                            }
                        }

                        IconButton {
                            iconText: "\uD83D\uDCF7"
                            iconSize: 18; buttonSize: 34
                            customColor: "#CCCCCC"

                            ToolTip {
                                visible: scHover.containsMouse
                                text: "\u622A\u56FE (Ctrl+S)"
                                delay: 500
                            }

                            MouseArea {
                                id: scHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            onClicked: { showOsd("\uD83D\uDCF7 Screenshot shortcut") }
                        }

                        IconButton {
                            iconText: "\u2261"
                            iconSize: 20; buttonSize: 34
                            customColor: "#CCCCCC"

                            ToolTip {
                                visible: plHover.containsMouse
                                text: "\u64AD\u653E\u5217\u8868 (L)"
                                delay: 500
                            }

                            MouseArea {
                                id: plHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            onClicked: root.playlistVisible = !root.playlistVisible
                        }

                        IconButton {
                            iconText: root.visibility === Window.FullScreen ? "\u2922" : "\u26F6"
                            iconSize: 20; buttonSize: 34
                            customColor: "#CCCCCC"

                            ToolTip {
                                visible: fsHover.containsMouse
                                text: root.visibility === Window.FullScreen ? "\u9000\u51FA\u5168\u5C4F (Esc)" : "\u5168\u5C4F (F)"
                                delay: 500
                            }

                            MouseArea {
                                id: fsHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            onClicked: toggleFullScreen()
                        }
                    }
                }

                Rectangle {
                    id: hoverTooltip
                    visible: false
                    y: progressBar.mapToItem(controlsOverlay, 0, 0).y - height - 4
                    width: hoverTimeText.implicitWidth + 16
                    height: hoverTimeText.implicitHeight + 8
                    radius: 4
                    color: Qt.rgba(0, 0, 0, 0.85)
                    z: 10

                    Text {
                        id: hoverTimeText
                        anchors.centerIn: parent
                        color: "#ffffff"
                        font.pixelSize: 11
                        font.family: "IBM Plex Sans"
                    }
                }
            }
        }

        PlaylistSidebar {
            id: playlistSidebar
            Layout.preferredWidth: root.playlistVisible ? 260 : 0
            Layout.fillHeight: true
            playlistModel: playlist
            loadAndPlayFunc: loadAndPlay
            formatTimeFunc: formatTime
            openFileDialog: fileDialog
            openUrlDialog: urlDialog

            visible: Layout.preferredWidth > 0
            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 250; easing.type: Easing.InOutQuad }
            }

            onItemDoubleClicked: function(idx) {
                playlist.currentIndex = idx
                loadAndPlay(idx)
            }
            onItemRemoveRequested: function(idx) { if (idx >= 0) playlist.remove(idx) }
        }
    }

    Rectangle {
        id: contextMenu
        property int currentIndex: -1
        function popup() { contextMenu.visible = true }
        function close() { contextMenu.visible = false }

        x: Math.min(videoMouseArea.mouseX, videoContainer.width - width - 8)
        y: Math.min(videoMouseArea.mouseY, videoContainer.height - height - 8)
        width: 220
        visible: false
        radius: 10
        color: ThemeManager.bgSidebar
        border.color: ThemeManager.border; border.width: 0.5

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
        }

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2
            topPadding: 4

            Repeater {
                model: [
                    { text: player.isPlaying ? qsTr("Pause") : qsTr("Play"), icon: player.isPlaying ? "\u23F8" : "\u25B6", action: function() { togglePlayPause(); contextMenu.close() } },
                    { text: qsTr("Stop"), icon: "\u25A0", action: function() { player.stop(); showOsd("Stopped"); contextMenu.close() } },
                    { type: "divider" },
                    { text: qsTr("Jump to Time..."), icon: "\u23F1", action: function() { jumpDialog.open(); contextMenu.close() } },
                    { text: qsTr("Add to Playlist"), icon: "+", action: function() { fileDialog.open(); contextMenu.close() } },
                    { type: "divider" },
                    { text: qsTr("Screenshot"), icon: "\uD83D\uDCF7", action: function() { showOsd("Screenshot shortcut"); contextMenu.close() } },
                    { text: qsTr("Fullscreen"), icon: "\u26F6", action: function() { toggleFullScreen(); contextMenu.close() } },
                    { type: "divider" },
                    { text: qsTr("Video Info"), icon: "\u2139", action: function() { videoInfoHud.visible = !videoInfoHud.visible; contextMenu.close() } }
                ]

                delegate: Item {
                    width: contextMenu.width - 12
                    height: modelData.type === "divider" ? 9 : 32

                    Rectangle {
                        visible: modelData.type === "divider"
                        width: contextMenu.width - 24; height: 1
                        color: ThemeManager.border; opacity: 0.3
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Rectangle {
                        visible: modelData.type !== "divider"
                        width: contextMenu.width - 12; height: 32; radius: 6
                        color: cmItemMa.containsMouse ? ThemeManager.hoverHighlight : "transparent"

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 8

                            Text {
                                text: modelData.icon || ""
                                font.pixelSize: 14; color: ThemeManager.textSecondary
                                anchors.verticalCenter: parent.verticalCenter; width: 20
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                text: modelData.text || ""
                                font.pixelSize: 13; font.family: "IBM Plex Sans"
                                color: ThemeManager.textPrimary
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        MouseArea {
                            id: cmItemMa
                            anchors.fill: parent; hoverEnabled: true
                            onClicked: if (modelData.action) modelData.action()
                        }
                    }
                }
            }
        }

        Timer {
            id: contextMenuCloseTimer
            interval: 100; repeat: false
            onTriggered: {
                if (!contextMenu.contains(Qt.point(videoMouseArea.mouseX, videoMouseArea.mouseY)))
                    contextMenu.visible = false
            }
        }

        onVisibleChanged: {
            if (visible) contextMenuCloseTimer.start()
        }
    }

    Rectangle {
        id: toastRect
        anchors.top: parent.top; anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastText.implicitWidth + 30; height: toastText.implicitHeight + 20
        color: ThemeManager.errorColor; radius: 4
        opacity: 0; visible: opacity > 0

        Text {
            id: toastText
            anchors.centerIn: parent; color: "#ffffff"; font.pixelSize: 14
        }

        SequentialAnimation {
            id: toastAnimation
            PauseAnimation { duration: 3000 }
            NumberAnimation { target: toastRect; property: "opacity"; to: 0; duration: 500 }
        }
    }

    JumpDialog {
        id: jumpDialog
        anchors.centerIn: parent
        player: player
        formatTimeFunc: formatTime
        onJumpRequested: function(target) {
            player.seek(target)
            showOsd("Jumped to " + formatTime(target))
        }
    }
}
