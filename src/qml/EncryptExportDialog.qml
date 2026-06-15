import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

Dialog {
    id: root
    width: 460
    height: 480
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property string inputPath: ""
    property string outputPath: ""

    readonly property int space1: 8
    readonly property int space2: 16
    readonly property int space3: 24

    header: Item {}
    footer: Item {}

    background: Rectangle {
        color: "#1a1a2e"
        radius: 12
        border.color: "#334FC3F7"
        border.width: 1
    }

    contentItem: Rectangle {
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                text: qsTr("加密导出设置")
                font.pixelSize: 16
                font.bold: true
                font.family: "IBM Plex Sans"
                color: "#ffffff"
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#333333"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: space1

                CheckBox {
                    id: encryptCheckbox
                    text: qsTr("加密导出")
                    checked: true
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    contentItem: Text {
                        text: encryptCheckbox.text
                        font: encryptCheckbox.font
                        color: "#ffffff"
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: encryptCheckbox.indicator.width + 8
                    }
                    indicator: Rectangle {
                        implicitWidth: 20
                        implicitHeight: 20
                        x: encryptCheckbox.leftPadding
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 4
                        color: encryptCheckbox.checked ? "#4FC3F7" : "#2a2a3e"
                        border.color: encryptCheckbox.checked ? "#4FC3F7" : "#555555"
                        border.width: 1.5

                        Text {
                            anchors.centerIn: parent
                            text: "\u2713"
                            color: "#ffffff"
                            font.pixelSize: 14
                            font.bold: true
                            visible: encryptCheckbox.checked
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: {
                        if (!encryptCheckbox.checked) return ""
                        var name = root.inputPath.split("/").pop().split("\\").pop()
                        if (name.length > 30) name = "..." + name.substring(name.length - 27)
                        return name
                    }
                    color: "#888888"
                    font.pixelSize: 11
                    font.family: "IBM Plex Sans"
                    visible: root.inputPath !== ""
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                visible: encryptCheckbox.checked

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 8
                    color: passwordRadio.checked ? "#1e3a5f" : "#252535"
                    border.color: passwordRadio.checked ? "#4FC3F7" : "#333344"
                    border.width: passwordRadio.checked ? 2 : 1

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: passwordRadio.checked = true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Rectangle {
                            width: 20; height: 20
                            radius: 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: "transparent"
                            border.color: passwordRadio.checked ? "#4FC3F7" : "#666666"
                            border.width: 2

                            Rectangle {
                                anchors.centerIn: parent
                                width: 10; height: 10
                                radius: 5
                                color: "#4FC3F7"
                                visible: passwordRadio.checked
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: qsTr("使用密码")
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "IBM Plex Sans"
                                color: passwordRadio.checked ? "#ffffff" : "#aaaaaa"
                            }
                            Text {
                                text: qsTr("输入密码加密，播放时需要输入相同密码")
                                font.pixelSize: 10
                                font.family: "IBM Plex Sans"
                                color: "#777777"
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RadioButton {
                        id: passwordRadio
                        checked: true
                        visible: false
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 6
                    visible: passwordRadio.checked

                    TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        implicitHeight: 36
                        echoMode: TextInput.Password
                        placeholderText: qsTr("输入密码 (至少8位)")
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#222233"
                            radius: 6
                            border.color: passwordField.activeFocus ? "#4FC3F7" : "#444455"
                            border.width: passwordField.activeFocus ? 2 : 1
                        }
                    }

                    TextField {
                        id: confirmPasswordField
                        Layout.fillWidth: true
                        implicitHeight: 36
                        echoMode: TextInput.Password
                        placeholderText: qsTr("确认密码")
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#222233"
                            radius: 6
                            border.color: confirmPasswordField.activeFocus ? "#4FC3F7" : "#444455"
                            border.width: confirmPasswordField.activeFocus ? 2 : 1
                        }
                    }

                    Text {
                        text: qsTr("忘记密码将无法恢复")
                        font.pixelSize: 10
                        font.family: "IBM Plex Sans"
                        color: "#FF8A65"
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 8
                    color: autoKeyRadio.checked ? "#1e3a5f" : "#252535"
                    border.color: autoKeyRadio.checked ? "#4FC3F7" : "#333344"
                    border.width: autoKeyRadio.checked ? 2 : 1

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: autoKeyRadio.checked = true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Rectangle {
                            width: 20; height: 20
                            radius: 10
                            anchors.verticalCenter: parent.verticalCenter
                            color: "transparent"
                            border.color: autoKeyRadio.checked ? "#4FC3F7" : "#666666"
                            border.width: 2

                            Rectangle {
                                anchors.centerIn: parent
                                width: 10; height: 10
                                radius: 5
                                color: "#4FC3F7"
                                visible: autoKeyRadio.checked
                            }
                        }

                        ColumnLayout {
                            spacing: 2
                            Text {
                                text: qsTr("自动生成密钥")
                                font.pixelSize: 13
                                font.bold: true
                                font.family: "IBM Plex Sans"
                                color: autoKeyRadio.checked ? "#ffffff" : "#aaaaaa"
                            }
                            Text {
                                text: qsTr("系统生成密钥，加密后显示，请妥善保存")
                                font.pixelSize: 10
                                font.family: "IBM Plex Sans"
                                color: "#777777"
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    RadioButton {
                        id: autoKeyRadio
                        checked: false
                        visible: false
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("密钥将在加密完成后显示，请妥善保存")
                    font.pixelSize: 10
                    font.family: "IBM Plex Sans"
                    color: "#FFB74D"
                    visible: autoKeyRadio.checked
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: space1
                visible: encryptionBridge.isProcessing

                Text {
                    text: qsTr("加密中...") + " " + Math.round(encryptionBridge.progress * 100) + "%"
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: "#ffffff"
                }

                ProgressBar {
                    Layout.fillWidth: true
                    value: encryptionBridge.progress
                    background: Rectangle {
                        color: "#333344"
                        radius: 3
                        implicitHeight: 6
                    }
                    contentItem: Rectangle {
                        width: parent.width * encryptionBridge.progress
                        height: 6
                        radius: 3
                        color: "#4FC3F7"
                    }
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    id: cancelButton
                    text: qsTr("取消")
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 36
                    enabled: !encryptionBridge.isProcessing
                    font.pixelSize: 13
                    font.family: "IBM Plex Sans"
                    background: Rectangle {
                        color: cancelButton.hovered ? "#3a3a4e" : "#2a2a3e"
                        radius: 6
                        border.color: "#444466"
                        border.width: 1
                    }
                    contentItem: Text {
                        text: cancelButton.text
                        font: cancelButton.font
                        color: "#cccccc"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (encryptionBridge.isProcessing) {
                            encryptionBridge.cancel()
                        }
                        root.close()
                    }
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: exportButton
                    text: qsTr("导出")
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 36
                    enabled: encryptCheckbox.checked && !encryptionBridge.isProcessing
                    font.pixelSize: 13
                    font.bold: true
                    font.family: "IBM Plex Sans"
                    background: Rectangle {
                        color: exportButton.enabled ? (exportButton.hovered ? "#3DB5D9" : "#4FC3F7") : "#2a2a3e"
                        radius: 6
                    }
                    contentItem: Text {
                        text: exportButton.text
                        font: exportButton.font
                        color: exportButton.enabled ? "#ffffff" : "#555555"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (!encryptCheckbox.checked) return

                        if (passwordRadio.checked) {
                            var password = passwordField.text.trim()
                            var confirmPassword = confirmPasswordField.text.trim()

                            if (password.length < 8) {
                                console.error("Password too short")
                                return
                            }

                            if (password !== confirmPassword) {
                                console.error("Passwords do not match")
                                return
                            }

                            encryptionBridge.startEncryption(root.inputPath, root.outputPath, true, password)
                        } else {
                            encryptionBridge.startEncryption(root.inputPath, root.outputPath, false, "")
                        }
                    }
                }
            }
        }
    }

    QMLEncryptionExporter {
        id: encryptionBridge

        onEncryptionFinished: function(keyString) {
            root.close()

            if (keyString && keyString.length > 0) {
                var component = Qt.createComponent("KeyStringDialog.qml")
                if (component.status === Component.Ready) {
                    var dialog = component.createObject(root, {
                        keyString: keyString
                    })
                    if (dialog) {
                        dialog.open()
                    }
                }
            }
        }

        onErrorOccurred: function(error) {
            console.error("Encryption failed:", error)
        }
    }
}
