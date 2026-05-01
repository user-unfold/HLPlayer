import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HLPlayer

Rectangle {
    id: root

    property var playlistModel: null
    property var loadAndPlayFunc: null
    property var formatTimeFunc: null
    property var openFileDialog: null
    property var openUrlDialog: null
    property int currentIndex: 0
    property bool isPlaying: false

    signal itemClicked(int index)
    signal itemDoubleClicked(int index)
    signal itemRemoveRequested(int index)
    signal itemPathCopyRequested(int index)

    color: ThemeManager.bgSidebar
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 8
                spacing: 4

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    placeholderText: qsTr("Search playlist...")
                    font.pixelSize: 12
                    color: ThemeManager.textPrimary

                    background: Rectangle {
                        radius: 6
                        color: Qt.rgba(1, 1, 1, 0.06)
                        border.color: searchField.activeFocus
                                      ? ThemeManager.accentColor
                                      : "transparent"
                        border.width: 1
                    }

                    onTextChanged: {
                        // Filtering implemented client-side via visible property
                    }
                }

                IconButton {
                    iconText: "+"
                    iconSize: 18
                    buttonSize: 30
                    isAccent: true
                    onClicked: if (root.openFileDialog) root.openFileDialog.open()
                }

                IconButton {
                    iconText: "\uD83C\uDF10"
                    iconSize: 14
                    buttonSize: 30
                    isAccent: true
                    onClicked: if (root.openUrlDialog) root.openUrlDialog.open()
                }

                IconButton {
                    iconText: "\u2715"
                    iconSize: 12
                    buttonSize: 30
                    customColor: ThemeManager.errorColor
                    visible: root.playlistModel && root.playlistModel.count > 0
                    onClicked: if (root.playlistModel) root.playlistModel.clear()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: ThemeManager.border
            opacity: 0.3
        }

        ListView {
            id: playlistListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.playlistModel
            spacing: 2

            delegate: Rectangle {
                width: ListView.view.width
                height: 56
                color: {
                    if (model.isPlaying) return Qt.rgba(1, 1, 1, 0.06)
                    if (itemMouse.containsMouse) return ThemeManager.hoverHighlight
                    return "transparent"
                }

                Rectangle {
                    visible: model.isPlaying
                    width: 3
                    height: parent.height
                    color: "#8B5CF6"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 8
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 40
                        radius: 4
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: model.title || qsTr("Unknown")
                            font.pixelSize: 13
                            font.family: "IBM Plex Sans"
                            color: model.isPlaying ? "#FFFFFF" : ThemeManager.textPrimary
                            elide: Text.ElideRight
                        }

                        Text {
                            text: root.formatTimeFunc && model.duration > 0
                                  ? root.formatTimeFunc(model.duration)
                                  : ""
                            font.pixelSize: 11
                            font.family: "IBM Plex Sans"
                            color: ThemeManager.textSecondary
                        }
                    }
                }

                MouseArea {
                    id: itemMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    onClicked: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            playlistContextMenu.targetIndex = index
                            playlistContextMenu.popup()
                        } else {
                            root.itemClicked(index)
                        }
                    }
                    onDoubleClicked: root.itemDoubleClicked(index)
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: ThemeManager.border
                    opacity: 0.15
                }
            }

            Text {
                anchors.centerIn: parent
                text: root.playlistModel && root.playlistModel.count > 0
                      ? qsTr("No match")
                      : qsTr("No files loaded\nClick + to add")
                color: ThemeManager.textSecondary
                opacity: 0.4
                font.pixelSize: 13
                horizontalAlignment: Text.AlignHCenter
                visible: playlistListView.count === 0
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: "transparent"
            visible: root.playlistModel && root.playlistModel.count > 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12

                Text {
                    text: root.playlistModel
                          ? root.playlistModel.count + " items"
                          : ""
                    font.pixelSize: 11
                    color: ThemeManager.textSecondary
                }

                Item { Layout.fillWidth: true }
            }
        }
    }

    Menu {
        id: playlistContextMenu
        property int targetIndex: -1

        MenuItem {
            text: qsTr("Remove")
            onTriggered: root.itemRemoveRequested(playlistContextMenu.targetIndex)
        }
        MenuItem {
            text: qsTr("Copy Path")
            onTriggered: root.itemPathCopyRequested(playlistContextMenu.targetIndex)
        }
        MenuItem {
            text: qsTr("Show in Folder")
            onTriggered: {
                var url = root.playlistModel.getUrl(playlistContextMenu.targetIndex)
                if (url) Qt.openUrlExternally(url.substring(0, url.lastIndexOf("/")))
            }
        }
    }
}
