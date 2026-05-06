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
    color: ThemeManager.surface

    property string currentTitle: ""
    property bool controlsVisible: true
    property real playbackSpeed: player.playbackRate
    property bool playlistVisible: true
    property real previousVolume: 1.0
    property bool isMuted: player.volume === 0
    property real pendingSeekValue: -1
    property string playbackMode: "loop"
    readonly property var allPlaybackModes: ["loop", "sequential", "random"]

    readonly property int space1: 8
    readonly property int space2: 16
    readonly property int space3: 24

    onPlaybackSpeedChanged: {
        showOsd("Speed: " + playbackSpeed.toFixed(2) + "x");
    }

    Timer {
        id: hideControlsTimer
        interval: 3000
        repeat: false
        onTriggered: {
            if (root.visibility === Window.FullScreen) {
                root.controlsVisible = false;
            }
        }
    }

    function resetHideTimer() {
        root.controlsVisible = true;
        if (root.visibility === Window.FullScreen) {
            hideControlsTimer.restart();
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
        onPositionChanged: resetHideTimer()
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.visibility === Window.FullScreen
        onActivated: {
            root.showNormal();
            root.controlsVisible = true;
            hideControlsTimer.stop();
        }
    }

    Shortcut { sequence: "Space"; onActivated: togglePlayPause() }
    Shortcut {
        sequence: "Left"
        onActivated: {
            if (!canSeek()) return;
            player.seek(Math.max(0, player.position - 5));
            showOsd("-5s");
        }
    }
    Shortcut {
        sequence: "Right"
        onActivated: {
            if (!canSeek()) return;
            player.seek(Math.min(player.duration, player.position + 5));
            showOsd("+5s");
        }
    }
    Shortcut { sequence: "Up"; onActivated: { player.volume = Math.min(1.0, player.volume + 0.05); showOsd("Vol: " + Math.round(player.volume*100) + "%"); } }
    Shortcut { sequence: "Down"; onActivated: { player.volume = Math.max(0.0, player.volume - 0.05); showOsd("Vol: " + Math.round(player.volume*100) + "%"); } }
    Shortcut { sequence: "F"; onActivated: toggleFullScreen() }
    Shortcut { sequence: "M"; onActivated: toggleMute() }
    Shortcut { sequence: "N"; onActivated: playNext() }
    Shortcut { sequence: "P"; onActivated: playPrev() }
    Shortcut { sequence: "Delete"; onActivated: if (playlistView.currentIndex >= 0) playlist.remove(playlistView.currentIndex) }

    function hasPlayableSource() {
        return player.source && player.source !== "";
    }

    function canSeek() {
        return hasPlayableSource() && player.duration > 0;
    }

    function togglePlayPause() {
        if (!hasPlayableSource()) {
            showOsd("No media loaded");
            return;
        }
        if (player.isPlaying) { player.pause(); showOsd("Paused"); }
        else { player.play(); showOsd("Playing"); }
    }
    function toggleFullScreen() {
        if (root.visibility === Window.FullScreen) {
            root.showNormal();
            root.controlsVisible = true;
            hideControlsTimer.stop();
        } else {
            root.showFullScreen();
            hideControlsTimer.restart();
        }
    }
    function toggleMute() {
        if (player.volume > 0) {
            previousVolume = player.volume;
            player.volume = 0;
            showOsd("Muted");
        } else {
            player.volume = previousVolume > 0 ? previousVolume : 1.0;
            showOsd("Unmuted");
        }
    }
    function playNext() {
        if (playlist.count === 0) return;
        var next = playlist.currentIndex + 1;
        if (next >= playlist.count) next = 0;
        playlist.currentIndex = next;
        loadAndPlay(next);
    }
    function playPrev() {
        if (playlist.count === 0) return;
        var prev = playlist.currentIndex - 1;
        if (prev < 0) prev = playlist.count - 1;
        playlist.currentIndex = prev;
        loadAndPlay(prev);
    }

    function stateLabel(state) {
        switch (state) {
        case 0: return PlayerI18nContext.tr("Idle")
        case 1: return PlayerI18nContext.tr("Opening")
        case 2: return PlayerI18nContext.tr("Prepared")
        case 3: return PlayerI18nContext.tr("Buffering")
        case 4: return PlayerI18nContext.tr("Playing")
        case 5: return PlayerI18nContext.tr("Paused")
        case 6: return PlayerI18nContext.tr("Error")
        case 7: return PlayerI18nContext.tr("End")
        case 8: return PlayerI18nContext.tr("Device Lost")
        default: return PlayerI18nContext.tr("Unknown")
        }
    }

    function formatTime(seconds) {
        if (!isFinite(seconds) || seconds < 0) return "00:00";
        var h = Math.floor(seconds / 3600);
        var m = Math.floor((seconds % 3600) / 60);
        var s = Math.floor(seconds % 60);
        var res = "";
        if (h > 0) {
            res += (h < 10 ? "0" : "") + h + ":";
        }
        res += (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
        return res;
    }

    function showOsd(text) {
        osdText.text = text;
        osdAnimation.stop();
        osdRect.opacity = 0.8;
        osdAnimation.start();
    }

    function showToast(text) {
        toastText.text = text;
        toastAnimation.stop();
        toastRect.opacity = 0.9;
        toastAnimation.start();
    }

    Component.onCompleted: {
        ThemeManager.theme = ThemeManager.Dark
    }

    PlaylistModel {
        id: playlist
    }

    QMLPlayer {
        id: player
        onVideoResolutionChanged: {
            if (player.videoWidth > 0 && player.videoHeight > 0) {
                var w = player.videoWidth
                var h = player.videoHeight
                var maxW = Screen.desktopAvailableWidth * 0.8
                var maxH = Screen.desktopAvailableHeight * 0.75
                if (w > maxW) {
                    var scale = maxW / w
                    w = Math.round(w * scale)
                    h = Math.round(h * scale)
                }
                if (h > maxH) {
                    var scale2 = maxH / h
                    w = Math.round(w * scale2)
                    h = Math.round(h * scale2)
                }
                videoArea.implicitWidth = Math.max(w, 320)
                videoArea.implicitHeight = Math.max(h, 240)
            }
        }
        onSourceChanged: {
            if (source) {
                var path = source
                if (path.startsWith("file:///"))
                    path = decodeURIComponent(path.substring(8).replace(/\\/g, "/"))
                root.currentTitle = path.split("/").pop()
            }
        }
        onErrorChanged: {
            if (error !== "") showToast(error);
        }
    }

    FileDialog {
        id: fileDialog
        title: PlayerI18nContext.tr("Open Media File")
        nameFilters: [
            "Media files (*.mp4 *.mkv *.avi *.mov *.wmv *.flv *.webm *.mp3 *.flac *.wav *.ogg *.aac *.m4a)",
            "All files (*)"
        ]
        onAccepted: {
            var urls = []
            for (var i = 0; i < selectedFiles.length; i++) {
                var raw = selectedFiles[i]
                var localPath = ""
                if (raw && raw.toLocalFile) localPath = raw.toLocalFile()
                else if (raw && raw.toString) localPath = raw.toString()
                else localPath = String(raw)
                if (localPath !== "") urls.push(localPath)
            }
            if (urls.length === 0) return
            playlist.addFiles(urls)
            playlist.currentIndex = playlist.count - urls.length
            loadAndPlay(playlist.currentIndex)
        }
    }

    function loadAndPlay(index) {
        var url = playlist.getUrl(index)
        if (!url) return
        var localUrl = url
        if (!localUrl.startsWith("file://"))
            localUrl = "file:///" + encodeURI(url.replace(/\\/g, "/"))
        player.source = localUrl
        player.play()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            color: "#000000"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    id: videoArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    VideoOutputItem {
                        id: videoOutput
                        anchors.fill: parent
                        clip: true
                        videoSink: player.videoSink
                    }

                    DropArea {
                        anchors.fill: parent
                        onDropped: (drop) => {
                            if (drop.hasUrls) {
                                var urls = [];
                                for (var i = 0; i < drop.urls.length; i++) {
                                    urls.push(drop.urls[i]);
                                }
                                playlist.addFiles(urls);
                                playlist.currentIndex = playlist.count - urls.length;
                                loadAndPlay(playlist.currentIndex);
                            }
                        }
                    }

                    Timer {
                        id: clickTimer
                        interval: 250
                        repeat: false
                        onTriggered: togglePlayPause()
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onDoubleClicked: {
                            clickTimer.stop()
                            toggleFullScreen()
                        }
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                contextMenu.popup()
                            } else {
                                clickTimer.start()
                            }
                        }
                        onWheel: (wheel) => {
                            if (wheel.modifiers & Qt.ControlModifier) {
                                if (!canSeek()) return;
                                var delta = wheel.angleDelta.y > 0 ? 5 : -5;
                                player.seek(Math.max(0, Math.min(player.duration, player.position + delta)));
                                showOsd((delta > 0 ? "+" : "") + delta + "s");
                            } else {
                                var vDelta = wheel.angleDelta.y > 0 ? 0.05 : -0.05;
                                player.volume = Math.max(0.0, Math.min(1.0, player.volume + vDelta));
                                showOsd("Vol: " + Math.round(player.volume*100) + "%");
                            }
                        }
                    }

                    Menu {
                        id: contextMenu
                        MenuItem { text: player.isPlaying ? "Pause" : "Play"; onTriggered: togglePlayPause() }
                        Menu {
                            title: "Speed"
                            MenuItem { text: "0.5x"; onTriggered: player.playbackRate = 0.5 }
                            MenuItem { text: "0.75x"; onTriggered: player.playbackRate = 0.75 }
                            MenuItem { text: "1.0x"; onTriggered: player.playbackRate = 1.0 }
                            MenuItem { text: "1.25x"; onTriggered: player.playbackRate = 1.25 }
                            MenuItem { text: "1.5x"; onTriggered: player.playbackRate = 1.5 }
                            MenuItem { text: "2.0x"; onTriggered: player.playbackRate = 2.0 }
                        }
                        MenuItem { text: root.isMuted ? "Unmute" : "Mute"; onTriggered: toggleMute() }
                        MenuItem { text: root.visibility === Window.FullScreen ? "Exit Fullscreen" : "Fullscreen"; onTriggered: toggleFullScreen() }
                        MenuSeparator {}
                        MenuItem {
                            text: {
                                var info = "";
                                if (player.videoWidth > 0)
                                    info += player.videoWidth + "x" + player.videoHeight;
                                if (player.fps > 0)
                                    info += (info ? " | " : "") + player.fps.toFixed(1) + " fps";
                                return info || "No media info";
                            }
                            enabled: false
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "transparent"
                        border.color: "#222222"
                        border.width: 1
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 12
                        text: root.currentTitle
                        color: "#ffffff"
                        font.pixelSize: 14
                        font.family: "IBM Plex Sans"
                        opacity: root.controlsVisible ? 0.8 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                        visible: root.currentTitle !== ""
                        style: Text.Outline
                        styleColor: "#000000"
                    }

                    Text {
                        anchors.centerIn: parent
                        text: PlayerI18nContext.tr("Drop files or click Open to play")
                        color: "#666666"
                        font.pixelSize: 18
                        font.family: "IBM Plex Sans"
                        visible: player.state === 0 && !root.currentTitle
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        visible: player.state === 1 || player.state === 3 // Opening or Buffering
                    }

                    // C1: Mini progress bar visible when controls are hidden
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 3
                        color: "#444444"
                        visible: !root.controlsVisible && player.duration > 0
                        z: 10
                        Rectangle {
                            width: parent.width * (player.duration > 0 ? player.position / player.duration : 0)
                            height: parent.height
                            color: ThemeManager.accentColor
                        }
                    }
                }

                // C3: Gradient backdrop behind floating controls
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.controlsVisible ? 120 : 0
                    opacity: root.controlsVisible ? 1.0 : 0.0
                    visible: opacity > 0
                    Behavior on Layout.preferredHeight { NumberAnimation { duration: 200 } }
                    Behavior on opacity { NumberAnimation { duration: 200 } }
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: "#C0000000" }
                    }

                    Rectangle {
                        id: controlsBar
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.bottomMargin: 8
                        height: 72
                        radius: 8
                        color: ThemeManager.surfaceVariant
                        opacity: 0.95

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 2

                            Slider {
                                id: progressSlider
                                Layout.fillWidth: true
                                Layout.topMargin: 2
                                height: 20
                                from: 0
                                to: player.duration > 0 ? player.duration : 1

                                // ── Seek state isolation ──────────────────────────
                                // When the user is dragging the slider, or the backend
                                // is still settling after a seek, break the value
                                // binding so that stale backend PTS cannot overwrite
                                // the user's chosen position.
                                property bool _seekDragging: false

                                Binding on value {
                                    when: !progressSlider._seekDragging
                                    value: player.position
                                }

                                onMoved: {
                                    if (!canSeek()) return;
                                    pendingSeekValue = value;
                                }

                                onPressedChanged: {
                                    if (pressed) {
                                        _seekDragging = true;
                                        return;
                                    }
                                    // ── Released ──
                                    if (!canSeek()) {
                                        pendingSeekValue = -1;
                                        _seekDragging = false;
                                        return;
                                    }
                                    // Calculate target from pending seek (if dragged) or visual position (if clicked)
                                    var target = pendingSeekValue >= 0 ? pendingSeekValue : valueAt(position);
                                    pendingSeekValue = -1;
                                    player.seek(target);
                                    _seekDragging = false;
                                }

                            background: Rectangle {
                                x: progressSlider.leftPadding
                                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                                width: progressSlider.availableWidth
                                height: 4
                                radius: 2
                                color: "#444444"
                                Rectangle {
                                    width: progressSlider.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: ThemeManager.accentColor
                                }
                            }
                            handle: Rectangle {
                                x: progressSlider.leftPadding + progressSlider.visualPosition * (progressSlider.availableWidth - width)
                                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                                width: 12
                                height: 12
                                radius: 6
                                color: progressSlider.pressed ? "#ffffff" : ThemeManager.accentColor
                            }

                            // C5: Hover time tooltip
                            MouseArea {
                                id: sliderHoverArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                                property real hoverTime: {
                                    if (!containsMouse || player.duration <= 0) return -1;
                                    var fraction = (mouseX - progressSlider.leftPadding) / progressSlider.availableWidth;
                                    fraction = Math.max(0, Math.min(1, fraction));
                                    return fraction * player.duration;
                                }
                            }

                            Rectangle {
                                id: hoverTooltip
                                visible: sliderHoverArea.containsMouse && sliderHoverArea.hoverTime >= 0
                                x: {
                                    var tipX = sliderHoverArea.mouseX - width / 2;
                                    return Math.max(0, Math.min(tipX, progressSlider.width - width));
                                }
                                y: -height - 4
                                width: hoverTimeText.implicitWidth + 12
                                height: hoverTimeText.implicitHeight + 8
                                radius: 4
                                color: "#CC000000"

                                Text {
                                    id: hoverTimeText
                                    anchors.centerIn: parent
                                    text: formatTime(sliderHoverArea.hoverTime)
                                    color: "#ffffff"
                                    font.pixelSize: 11
                                    font.family: "IBM Plex Sans"
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: space1

                            Button {
                                id: prevBtn
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                flat: true
                                background: Rectangle {
                                    radius: 18
                                    color: prevBtn.hovered ? ThemeManager.accentColor : ThemeManager.onSurface
                                    opacity: prevBtn.hovered ? 0.3 : 0.15
                                }
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    width: 20; height: 20
                                    source: "qrc:/icons/backwardBtn.png"
                                    fillMode: Image.PreserveAspectFit
                                }
                                onClicked: playPrev()
                            }

                            Button {
                                id: playPauseBtn
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                flat: true
                                background: Rectangle {
                                    radius: 18
                                    color: playPauseBtn.hovered ? ThemeManager.accentColor : ThemeManager.onSurface
                                    opacity: playPauseBtn.hovered ? 0.3 : 0.15
                                }
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    width: 22; height: 22
                                    source: player.isPlaying ? "qrc:/icons/pauseBtn.png" : "qrc:/icons/playBtn.png"
                                    fillMode: Image.PreserveAspectFit
                                }
                                onClicked: togglePlayPause()
                            }

                            Button {
                                id: nextBtn
                                Layout.preferredWidth: 36
                                Layout.preferredHeight: 36
                                flat: true
                                background: Rectangle {
                                    radius: 18
                                    color: nextBtn.hovered ? ThemeManager.accentColor : ThemeManager.onSurface
                                    opacity: nextBtn.hovered ? 0.3 : 0.15
                                }
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    width: 20; height: 20
                                    source: "qrc:/icons/forwardBtn.png"
                                    fillMode: Image.PreserveAspectFit
                                }
                                onClicked: playNext()
                            }

                            Text {
                                text: formatTime(player.position) + " / " + formatTime(player.duration)
                                font.pixelSize: 12
                                font.family: "IBM Plex Sans"
                                color: ThemeManager.onSurface
                                opacity: 0.7
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                id: speedBtn
                                flat: true
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 36
                                onClicked: speedPopup.open()

                                background: Rectangle {
                                    radius: 4
                                    color: speedBtn.hovered ? ThemeManager.surface : "transparent"
                                }

                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: player.playbackRate.toFixed(2) + "x"
                                    font.pixelSize: 12
                                    font.family: "IBM Plex Sans"
                                    color: Math.abs(player.playbackRate - 1.0) > 0.01
                                           ? ThemeManager.accentColor
                                           : ThemeManager.onSurface
                                }

                                Popup {
                                    id: speedPopup
                                    x: speedBtn.width / 2 - width / 2
                                    y: -height - 4
                                    width: 80
                                    padding: 4

                                    background: Rectangle {
                                        color: ThemeManager.surfaceVariant
                                        radius: 6
                                        border.color: ThemeManager.onSurface
                                        border.width: 0.5
                                        opacity: 0.95
                                    }

                                    contentItem: Column {
                                        spacing: 0
                                        Repeater {
                                            model: [0.5, 0.75, 1.0, 1.25, 1.5, 2.0]
                                            delegate: Rectangle {
                                                width: 72
                                                height: 28
                                                radius: 4
                                                color: {
                                                    var isActive = Math.abs(player.playbackRate - modelData) < 0.01;
                                                    if (isActive) return ThemeManager.accentColor;
                                                    if (speedItemMa.containsMouse) return ThemeManager.surface;
                                                    return "transparent";
                                                }
                                                opacity: Math.abs(player.playbackRate - modelData) < 0.01 ? 0.3 : 1.0

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: modelData.toFixed(2) + "x"
                                                    font.pixelSize: 12
                                                    color: ThemeManager.onSurface
                                                    font.bold: Math.abs(player.playbackRate - modelData) < 0.01
                                                }

                                                MouseArea {
                                                    id: speedItemMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    onClicked: {
                                                        player.playbackRate = modelData;
                                                        speedPopup.close();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Button {
                                id: playbackModeBtn
                                flat: true
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28

                                function modeIcon(mode) {
                                    if (mode === "loop") return "qrc:/icons/playbackModeLoop.png"
                                    if (mode === "sequential") return "qrc:/icons/sequentialBtn.png"
                                    if (mode === "random") return "qrc:/icons/shuffleBtn.png"
                                    return "qrc:/icons/playbackModeLoop.png"
                                }
                                function modeLabel(mode) {
                                    if (mode === "loop") return PlayerI18nContext.tr("Loop")
                                    if (mode === "sequential") return PlayerI18nContext.tr("Sequential")
                                    if (mode === "random") return PlayerI18nContext.tr("Shuffle")
                                    return ""
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: playbackModeBtn.hovered ? ThemeManager.surface : "transparent"
                                }
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    width: 20; height: 20
                                    source: playbackModeBtn.modeIcon(root.playbackMode)
                                    fillMode: Image.PreserveAspectFit
                                }
                                onClicked: playbackModePopup.open()

                                Popup {
                                    id: playbackModePopup
                                    x: playbackModeBtn.width / 2 - width / 2
                                    y: -height - 4
                                    width: 120
                                    padding: 4
                                    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                                    background: Rectangle {
                                        color: ThemeManager.surfaceVariant
                                        radius: 6
                                        border.color: ThemeManager.onSurface
                                        border.width: 0.5
                                        opacity: 0.95
                                    }

                                    contentItem: Column {
                                        spacing: 0
                                        Repeater {
                                            model: root.allPlaybackModes
                                            delegate: Rectangle {
                                                width: 112
                                                height: 32
                                                radius: 4
                                                color: {
                                                    if (modelData === root.playbackMode)
                                                        return ThemeManager.accentColor
                                                    if (modeItemMa.containsMouse)
                                                        return ThemeManager.surface
                                                    return "transparent"
                                                }
                                                opacity: modelData === root.playbackMode ? 0.3 : 1.0

                                                RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 8
                                                    anchors.rightMargin: 8
                                                    spacing: 6

                                                    Image {
                                                        Layout.preferredWidth: 18
                                                        Layout.preferredHeight: 18
                                                        source: playbackModeBtn.modeIcon(modelData)
                                                        fillMode: Image.PreserveAspectFit
                                                    }
                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: playbackModeBtn.modeLabel(modelData)
                                                        font.pixelSize: 12
                                                        color: modelData === root.playbackMode
                                                               ? ThemeManager.accentColor
                                                               : ThemeManager.onSurface
                                                        elide: Text.ElideRight
                                                    }
                                                }

                                                MouseArea {
                                                    id: modeItemMa
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    onClicked: {
                                                        root.playbackMode = modelData
                                                        playbackModePopup.close()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Button {
                                flat: true
                                Layout.preferredWidth: 28
                                Layout.preferredHeight: 28
                                onClicked: toggleMute()
                                contentItem: Image {
                                    anchors.centerIn: parent
                                    width: 20; height: 20
                                    source: "qrc:/icons/volumnButton.png"
                                    fillMode: Image.PreserveAspectFit
                                    opacity: root.isMuted ? 0.4 : 1.0
                                }
                            }

                            Slider {
                                id: volumeSlider
                                Layout.preferredWidth: 80
                                height: 20
                                from: 0.0
                                to: 1.0
                                value: player.volume
                                onMoved: {
                                    player.volume = value;
                                    if(value > 0) previousVolume = value;
                                }
                                background: Rectangle {
                                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                    width: volumeSlider.availableWidth
                                    height: 3
                                    radius: 1
                                    color: "#444444"
                                    Rectangle {
                                        width: volumeSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: 1
                                        color: ThemeManager.primary
                                    }
                                }
                                handle: Rectangle {
                                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: ThemeManager.primary
                                }
                            }

                            Button {
                                flat: true
                                Layout.preferredWidth: 32
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "\u2261" // Playlist icon ≡
                                    color: ThemeManager.onSurface
                                }
                                onClicked: root.playlistVisible = !root.playlistVisible
                            }

                            Button {
                                id: vsrStudioBtn
                                flat: true
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 36
                                background: Rectangle {
                                    radius: 4
                                    color: vsrStudioBtn.hovered ? ThemeManager.surface : "transparent"
                                }
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "VSR"
                                    font.pixelSize: 10
                                    font.bold: true
                                    color: ThemeManager.accentColor
                                }
onClicked: {
    console.log("VSR: Creating OfflineVSRStudio component...");
    var component = Qt.createComponent("../OfflineVSRStudio.qml");
    if (component.status === Component.Ready) {
        console.log("VSR: Component ready, creating window...");
        var win = component.createObject(root);
        if (win) {
            console.log("VSR: Window created successfully, showing...");
            win.show();
        } else {
            console.error("VSR: Failed to create window from component");
            showToast("Failed to open VSR Studio: Window creation failed");
        }
    } else {
        console.error("VSR: Component not ready. Status:", component.status, "Error:", component.errorString());
        showToast("Failed to open VSR Studio: " + component.errorString());
    }
}
                            }

                            Button {
                                id: cameraRecordingBtn
                                flat: true
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 36
                                background: Rectangle {
                                    radius: 4
                                    color: cameraRecordingBtn.hovered ? ThemeManager.surface : "transparent"
                                }
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "Cam"
                                    font.pixelSize: 10
                                    font.bold: true
                                    color: ThemeManager.accentColor
                                }
                                onClicked: {
                                    console.log("Camera: Creating CameraRecordingPage...");
                                    var component = Qt.createComponent("file:///D:/HLPlayer/src/camera/qml/CameraRecordingPage.qml");
                                    if (component.status === Component.Error) {
                                        console.error("Camera: Component error:", component.errorString());
                                        showToast("Failed: " + component.errorString());
                                    } else if (component.status === Component.Ready) {
                                        var win = component.createObject(root);
                                        if (win) {
                                            console.log("Camera: Window created, showing...");
                                            win.show();
                                        } else {
                                            console.error("Camera: Failed to create window");
                                            showToast("Failed to open Camera Recording");
                                        }
                                    } else {
                                        console.error("Camera: Component error:", component.errorString());
                                        showToast("Failed to open Camera Recording: " + component.errorString());
                                    }
                                }
                            }

                            Button {
                                id: streamingBtn
                                flat: true
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 36
                                background: Rectangle {
                                    radius: 4
                                    color: streamingBtn.hovered ? ThemeManager.surface : "transparent"
                                }
                                contentItem: Text {
                                    anchors.centerIn: parent
                                    text: "Str"
                                    font.pixelSize: 10
                                    font.bold: true
                                    color: ThemeManager.accentColor
                                }
                                onClicked: {
                                    console.log("Streaming: Creating StreamingPage...");
                                    var component = Qt.createComponent("file:///D:/HLPlayer/src/camera/qml/StreamingPage.qml");
                                    if (component.status === Component.Error) {
                                        console.error("Streaming: Component error:", component.errorString());
                                        showToast("Failed: " + component.errorString());
                                    } else if (component.status === Component.Ready) {
                                        var win = component.createObject(root);
                                        if (win) {
                                            console.log("Streaming: Window created, showing...");
                                            win.show();
                                        } else {
                                            console.error("Streaming: Failed to create window");
                                            showToast("Failed to open Streaming");
                                        }
                                    } else {
                                        console.error("Streaming: Component error:", component.errorString());
                                        showToast("Failed to open Streaming: " + component.errorString());
                                    }
                                }
                            }
                        }
                    }
                    } // controlsBar Rectangle
                } // gradient backdrop Rectangle
            }
        }

        Rectangle {
            id: playlistPanel
            Layout.preferredWidth: root.playlistVisible ? 280 : 0
            Layout.fillHeight: true
            color: ThemeManager.surfaceVariant
            opacity: 0.95
            visible: Layout.preferredWidth > 0
            clip: true
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 0
                width: 280

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    Layout.leftMargin: 12
                    Layout.rightMargin: 8

                    Text {
                        text: PlayerI18nContext.tr("Playlist")
                        font.pixelSize: 15
                        font.bold: true
                        font.family: "IBM Plex Sans"
                        color: ThemeManager.onSurface
                    }

                    Item { Layout.fillWidth: true }

                    Text {
                        text: playlist.count
                        font.pixelSize: 12
                        color: ThemeManager.onSurface
                        opacity: 0.5
                    }

                    Button {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        flat: true
                        background: Rectangle { radius: 6; color: parent.hovered ? ThemeManager.surface : "transparent" }
                        contentItem: Text {
                            anchors.centerIn: parent
                            text: "+"
                            font.pixelSize: 16
                            font.bold: true
                            color: ThemeManager.accentColor
                        }
                        onClicked: fileDialog.open()
                    }

                    Button {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        flat: true
                        visible: playlist.count > 0
                        background: Rectangle { radius: 6; color: parent.hovered ? ThemeManager.surface : "transparent" }
                        contentItem: Text {
                            anchors.centerIn: parent
                            text: "\u2715"
                            font.pixelSize: 12
                            color: ThemeManager.errorColor
                        }
                        onClicked: playlist.clear()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: ThemeManager.onSurface
                    opacity: 0.1
                }

                ListView {
                    id: playlistView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: playlist

                    delegate: Rectangle {
                        width: playlistView.width
                        height: 44
                        color: {
                            if (model.isPlaying) return ThemeManager.accentColor
                            if (mouseArea.containsMouse) return ThemeManager.surface
                            return "transparent"
                        }
                        opacity: model.isPlaying ? 0.15 : 1.0

                        Rectangle {
                            visible: model.isPlaying
                            width: 3
                            height: parent.height
                            color: ThemeManager.accentColor
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 8
                            anchors.topMargin: 4
                            text: model.title
                            font.pixelSize: 13
                            font.family: "IBM Plex Sans"
                            color: model.isPlaying ? ThemeManager.accentColor : ThemeManager.onSurface
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 4
                            text: model.isPlaying ? stateLabel(player.state) : ""
                            font.pixelSize: 10
                            color: ThemeManager.accentColor
                            opacity: 0.7
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onDoubleClicked: {
                                playlist.currentIndex = index
                                loadAndPlay(index)
                            }
                        }

                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: ThemeManager.onSurface
                            opacity: 0.05
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: PlayerI18nContext.tr("No files loaded\nClick + to add")
                        color: ThemeManager.onSurface
                        opacity: 0.3
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        visible: playlist.count === 0
                    }
                }
            }
        }
    }

    // OSD Overlay
    Rectangle {
        id: osdRect
        anchors.centerIn: parent
        width: osdText.implicitWidth + 40
        height: osdText.implicitHeight + 20
        color: "#80000000"
        radius: 8
        opacity: 0.0
        visible: opacity > 0

        Text {
            id: osdText
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 24
            font.bold: true
        }

        NumberAnimation {
            id: osdAnimation
            target: osdRect
            property: "opacity"
            to: 0.0
            duration: 1000
            easing.type: Easing.InQuad
        }
    }

    // Toast Notification
    Rectangle {
        id: toastRect
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        width: toastText.implicitWidth + 30
        height: toastText.implicitHeight + 20
        color: ThemeManager.errorColor
        radius: 4
        opacity: 0.0
        visible: opacity > 0

        Text {
            id: toastText
            anchors.centerIn: parent
            color: "#ffffff"
            font.pixelSize: 14
        }

        SequentialAnimation {
            id: toastAnimation
            PauseAnimation { duration: 3000 }
            NumberAnimation {
                target: toastRect
                property: "opacity"
                to: 0.0
                duration: 500
            }
        }
    }
}
