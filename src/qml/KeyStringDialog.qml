import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 9999

    property string keyString: ""
    property var encryptionBridge: null

    signal closed()

    // Modal overlay
    Rectangle {
        anchors.fill: parent
        color: "#80000000"
        MouseArea {
            anchors.fill: parent
            onClicked: { /* block clicks behind */ }
        }
    }

    // Dialog card
    Rectangle {
        width: 500
        height: 240
        anchors.centerIn: parent
        color: "#cc1a1a2e"
        radius: 12
        border.color: "#334FC3F7"
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 16

            Text {
                text: qsTr("密钥已生成 (Key Generated)")
                font.pixelSize: 16
                font.bold: true
                color: "#4FC3F7"
            }

            Text {
                text: qsTr("请妥善保存此密钥，丢失将无法解密文件\nPlease save this key securely, you cannot decrypt files without it")
                font.pixelSize: 12
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
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
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
                    onClicked: keyText.selectAll()
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12

                // Copy button
                Button {
                    text: qsTr("复制密钥 (Copy Key)")
                    Layout.preferredWidth: 150
                    font.pixelSize: 12
                    background: Rectangle {
                        color: copyBtn.hovered ? "#3DB5D9" : "#4FC3F7"
                        radius: 4
                    }
                    contentItem: Text {
                        text: copyBtn.text
                        font: copyBtn.font
                        color: "#ffffff"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (encryptionBridge) {
                            encryptionBridge.copyToClipboard(root.keyString)
                        }
                    }
                    id: copyBtn
                }

                // Confirm button
                Button {
                    text: qsTr("我已保存 (I've saved it)")
                    Layout.preferredWidth: 150
                    font.pixelSize: 12
                    background: Rectangle {
                        color: confirmBtn.hovered ? "#444444" : "#333333"
                        radius: 4
                    }
                    contentItem: Text {
                        text: confirmBtn.text
                        font: confirmBtn.font
                        color: "#cccccc"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        root.visible = false
                        root.closed()
                    }
                    id: confirmBtn
                }
            }
        }
    }
}
