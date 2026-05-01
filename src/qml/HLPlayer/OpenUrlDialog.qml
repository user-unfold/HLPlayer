import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

Dialog {
    id: root
    title: PlayerI18nContext.tr("Open Network Stream")
    modal: true
    anchors.centerIn: parent
    width: 520
    height: 120
    closePolicy: Popup.NoAutoClose

    property var playlist: null
    property var loadAndPlayFunction: null
    property var urlHistory: UrlHistory {}
    property bool isValidUrl: true

    readonly property int space1: 8
    readonly property int space2: 16

    ColumnLayout {
        anchors.fill: parent
        spacing: space2

        RowLayout {
            Layout.fillWidth: true
            spacing: space1

            ComboBox {
                id: protocolCombo
                Layout.preferredWidth: 100
                model: ["Auto", "RTSP", "RTMP", "RTMPS", "HTTP", "HTTPS", "HLS", "UDP", "RTP"]

                background: Rectangle {
                    implicitWidth: 100
                    implicitHeight: 36
                    radius: 4
                    color: protocolCombo.pressed ? ThemeManager.surface
                           : protocolCombo.hovered ? ThemeManager.surface : ThemeManager.surfaceVariant
                    border.color: ThemeManager.onSurface
                    border.width: protocolCombo.visualFocus ? 1 : 0.5
                    opacity: protocolCombo.pressed ? 0.8 : 1.0

                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 4
                        radius: 1
                        color: ThemeManager.onSurface
                        opacity: 0.5
                    }
                }

                contentItem: Text {
                    text: protocolCombo.displayText
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 10
                    rightPadding: 24
                }

                popup: Popup {
                    y: protocolCombo.height
                    width: protocolCombo.width
                    height: Math.min(32 * 9 + 8, 300)
                    padding: 4

                    background: Rectangle {
                        color: ThemeManager.surfaceVariant
                        radius: 4
                        border.color: ThemeManager.onSurface
                        border.width: 0.5
                        opacity: 0.95
                    }

                    contentItem: ListView {
                        clip: true
                        model: protocolCombo.popup.visible ? protocolCombo.delegateModel : null
                        currentIndex: protocolCombo.highlightedIndex
                        ScrollBar.vertical: ScrollBar {}

                        delegate: ItemDelegate {
                            width: protocolCombo.width
                            height: 32
                            highlighted: protocolCombo.highlightedIndex === index

                            background: Rectangle {
                                color: highlighted ? ThemeManager.accentColor
                                       : protocolItemMouseArea.containsMouse ? ThemeManager.surface : "transparent"
                                opacity: highlighted ? 0.3 : 1.0
                            }

                            contentItem: Text {
                                text: modelData
                                font.pixelSize: 13
                                font.family: "IBM Plex Sans"
                                color: ThemeManager.onSurface
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 10
                            }

                            MouseArea {
                                id: protocolItemMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    protocolCombo.currentIndex = index
                                    protocolCombo.popup.close()
                                }
                            }
                        }
                    }
                }
            }

            TextField {
                id: urlInput
                Layout.fillWidth: true
                placeholderText: PlayerI18nContext.tr("Enter stream URL...")
                selectByMouse: true

                background: Rectangle {
                    implicitHeight: 36
                    radius: 4
                    color: urlInput.activeFocus ? ThemeManager.surface : ThemeManager.surfaceVariant
                    border.color: {
                        if (!root.isValidUrl && urlInput.text !== "") return ThemeManager.errorColor
                        return urlInput.activeFocus ? ThemeManager.accentColor : ThemeManager.onSurface
                    }
                    border.width: urlInput.activeFocus || (!root.isValidUrl && urlInput.text !== "") ? 1 : 0.5
                    opacity: urlInput.activeFocus ? 0.9 : 0.8
                }

                color: ThemeManager.onSurface
                font.pixelSize: 13
                font.family: "IBM Plex Sans"
                leftPadding: 10
                rightPadding: 10
                topPadding: 8
                bottomPadding: 8

                onTextChanged: root.isValidUrl = true
                onAccepted: root.openStream()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: urlHistory.count > 0

            Text {
                Layout.fillWidth: true
                text: PlayerI18nContext.tr("Recent:")
                font.pixelSize: 12
                font.family: "IBM Plex Sans"
                color: ThemeManager.onSurface
                opacity: 0.7
            }

            Button {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 24
                text: PlayerI18nContext.tr("Clear")
                flat: true
                font.pixelSize: 11
                font.family: "IBM Plex Sans"

                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? ThemeManager.surface : "transparent"
                    border.color: ThemeManager.onSurface
                    border.width: 0.5
                    opacity: parent.hovered ? 0.6 : 0.4
                }

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: parent.font.pixelSize
                    font.family: parent.font.family
                    color: ThemeManager.onSurface
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: urlHistory.clearAll()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(120, urlHistory.count * 32)
            Layout.maximumHeight: 120
            clip: true
            model: urlHistory
            spacing: 4
            visible: urlHistory.count > 0

            delegate: Rectangle {
                width: ListView.view.width
                height: 32
                radius: 4
                color: historyMouseArea.containsMouse ? ThemeManager.surface : ThemeManager.surfaceVariant
                border.color: ThemeManager.onSurface
                border.width: 0.5
                opacity: historyMouseArea.containsMouse ? 0.9 : 0.8

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: model.label ? model.label : model.url
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: ThemeManager.onSurface
                        elide: Text.ElideMiddle
                    }

                    Button {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        text: "×"
                        flat: true
                        font.pixelSize: 16
                        font.family: "IBM Plex Sans"
                        font.bold: true

                        background: Rectangle {
                            radius: 2
                            color: deleteMouseArea.containsMouse ? ThemeManager.accentColor : "transparent"
                            opacity: deleteMouseArea.containsMouse ? 0.3 : 0.5
                        }

                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: parent.font.pixelSize
                            font.family: parent.font.family
                            color: ThemeManager.onSurface
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            id: deleteMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: urlHistory.removeUrl(index)
                            onPressed: mouse.accepted = true
                        }
                    }
                }

                MouseArea {
                    id: historyMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        var fullUrl = model.url
                        var protocol = ""
                        if (fullUrl.startsWith("rtsp://")) protocol = "RTSP"
                        else if (fullUrl.startsWith("rtmp://")) protocol = "RTMP"
                        else if (fullUrl.startsWith("rtmps://")) protocol = "RTMPS"
                        else if (fullUrl.startsWith("udp://")) protocol = "UDP"
                        else if (fullUrl.startsWith("rtp://")) protocol = "RTP"
                        else if (fullUrl.startsWith("http://")) protocol = "HTTP"
                        else if (fullUrl.startsWith("https://")) {
                            if (fullUrl.includes(".m3u8")) protocol = "HLS"
                            else protocol = "HTTPS"
                        }

                        if (protocol !== "") {
                            var protocolIndex = protocolCombo.find(protocol)
                            if (protocolIndex >= 0) protocolCombo.currentIndex = protocolIndex
                        }

                        var inputPart = fullUrl
                        var prefixes = ["rtsp://", "rtmp://", "rtmps://", "http://", "https://", "udp://", "rtp://"]
                        var stripped = true
                        while (stripped) {
                            stripped = false
                            for (var i = 0; i < prefixes.length; i++) {
                                if (inputPart.startsWith(prefixes[i])) {
                                    inputPart = inputPart.substring(prefixes[i].length)
                                    stripped = true
                                    break
                                }
                            }
                        }
                        urlInput.text = inputPart
                    }
                    onPressed: mouse.accepted = true
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }


        RowLayout {
            Layout.fillWidth: true
            spacing: space1

            Item { Layout.fillWidth: true }

            Button {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 32
                text: PlayerI18nContext.tr("Cancel")
                flat: true

                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? ThemeManager.surface : "transparent"
                    border.color: ThemeManager.onSurface
                    border.width: 0.5
                    opacity: parent.hovered ? 0.8 : 0.5
                }

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.close()
            }

            Button {
                Layout.preferredWidth: 80
                Layout.preferredHeight: 32
                text: PlayerI18nContext.tr("Open")
                flat: true

                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? ThemeManager.accentColor : ThemeManager.surfaceVariant
                    border.color: ThemeManager.accentColor
                    border.width: 1
                    opacity: parent.pressed ? 0.6 : 1.0
                }

                contentItem: Text {
                    text: parent.text
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    color: ThemeManager.onSurface
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: root.openStream()
            }
        }
    }

    function openStream() {
        var input = urlInput.text.trim()
        if (input === "") {
            root.isValidUrl = false
            urlInput.placeholderText = PlayerI18nContext.tr("URL cannot be empty")
            return
        }

        var protocol = protocolCombo.currentText
        var url = input

        if (!input.includes("://")) {
            var prefix = ""
            switch (protocol) {
                case "RTSP": prefix = "rtsp://"; break
                case "RTMP": prefix = "rtmp://"; break
                case "RTMPS": prefix = "rtmps://"; break
                case "HTTP": prefix = "http://"; break
                case "HTTPS": prefix = "https://"; break
                case "HLS": prefix = "https://"; break
                case "UDP": prefix = "udp://"; break
                case "RTP": prefix = "rtp://"; break
                default: prefix = "https://"; break
            }
            url = prefix + input
        }

        if (!url.includes("://")) {
            root.isValidUrl = false
            urlInput.placeholderText = PlayerI18nContext.tr("Invalid URL format")
            return
        }

        root.isValidUrl = true
        urlHistory.addUrl(url, protocol)
        playlist.add(url)
        playlist.currentIndex = playlist.count - 1
        loadAndPlayFunction(playlist.currentIndex)
        root.close()
    }
}
