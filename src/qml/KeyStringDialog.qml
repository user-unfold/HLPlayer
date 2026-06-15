import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: root
    width: 500
    height: 240
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string keyString: ""

    readonly property int space1: 8
    readonly property int space2: 16
    readonly property int space3: 24

    title: qsTr("密钥已生成 (Key Generated)")

    background: Rectangle {
        color: "#cc1a1a2e"
        radius: 12
        border.color: "#334FC3F7"
        border.width: 1
    }

    contentItem: Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: root.space2
            spacing: root.space2

            Text {
                text: qsTr("请妥善保存此密钥，丢失将无法解密文件 (Please save this key securely, you cannot decrypt files without it)")
                font.pixelSize: 12
                font.family: "IBM Plex Sans"
                color: "#FFB74D"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            // Key string display
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: "#333333"
                radius: 4
                border.color: "#444444"
                border.width: 1

                Text {
                    id: keyText
                    anchors.fill: parent
                    anchors.leftMargin: root.space2
                    anchors.rightMargin: root.space2
                    text: root.keyString
                    font.pixelSize: 13
                    font.family: "monospace"
                    color: "#ffffff"
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideNone
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        keyText.selectAll()
                    }
                }
            }

            // Copy button
            Button {
                id: copyButton
                text: qsTr("复制密钥 (Copy Key)")
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 140
                font.pixelSize: 12
                font.family: "IBM Plex Sans"
                background: Rectangle {
                    color: copyButton.hovered ? "#3DB5D9" : "#4FC3F7"
                    radius: 4
                }
                contentItem: Text {
                    text: copyButton.text
                    font: copyButton.font
                    color: "#ffffff"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    clipboard.text = root.keyString
                }
            }

            Item { Layout.fillHeight: true }

            // Confirm button
            Button {
                id: confirmButton
                text: qsTr("我已保存 (I've saved it)")
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 140
                font.pixelSize: 12
                font.family: "IBM Plex Sans"
                background: Rectangle {
                    color: confirmButton.hovered ? "#444444" : "#333333"
                    radius: 4
                }
                contentItem: Text {
                    text: confirmButton.text
                    font: confirmButton.font
                    color: "#cccccc"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: {
                    root.close()
                }
            }
        }
    }

    Clipboard {
        id: clipboard
    }
}