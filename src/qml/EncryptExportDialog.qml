import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

Dialog {
    id: root
    width: 420
    height: 360
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    title: qsTr("加密导出设置 / Encrypt Export Settings")

    property string inputPath: ""
    property string outputPath: ""

    readonly property int space1: 8
    readonly property int space2: 16
    readonly property int space3: 24

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

            // Encrypt checkbox
            CheckBox {
                id: encryptCheckbox
                text: qsTr("加密导出 (Encrypt Export)")
                Layout.fillWidth: true
                font.pixelSize: 13
                font.family: "IBM Plex Sans"
                contentItem: Text {
                    text: encryptCheckbox.text
                    font: encryptCheckbox.font
                    color: "#ffffff"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: encryptCheckbox.indicator.width + root.space1
                }
            }

            // Key mode radio buttons (shown when encrypt checked)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.space1
                visible: encryptCheckbox.checked

                ButtonGroup {
                    id: keyModeGroup
                    buttons: [passwordRadio, autoKeyRadio]
                }

                RadioButton {
                    id: passwordRadio
                    text: qsTr("使用密码 (Use Password)")
                    checked: true
                    Layout.fillWidth: true
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    contentItem: Text {
                        text: passwordRadio.text
                        font: passwordRadio.font
                        color: "#cccccc"
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: passwordRadio.indicator.width + root.space1
                    }
                }

                // Password fields
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.space3
                    spacing: root.space1
                    visible: passwordRadio.checked

                    Text {
                        text: qsTr("密码 (Password)")
                        font.pixelSize: 11
                        font.family: "IBM Plex Sans"
                        color: "#999999"
                    }

                    TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        height: 32
                        echoMode: TextInput.Password
                        placeholderText: qsTr("输入密码 (Enter password)")
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#333333"
                            radius: 4
                            border.color: passwordField.activeFocus ? "#4FC3F7" : "#444444"
                            border.width: passwordField.activeFocus ? 2 : 1
                        }
                    }

                    Text {
                        text: qsTr("确认密码 (Confirm Password)")
                        font.pixelSize: 11
                        font.family: "IBM Plex Sans"
                        color: "#999999"
                    }

                    TextField {
                        id: confirmPasswordField
                        Layout.fillWidth: true
                        height: 32
                        echoMode: TextInput.Password
                        placeholderText: qsTr("再次输入密码 (Re-enter password)")
                        font.pixelSize: 12
                        font.family: "IBM Plex Sans"
                        color: "#ffffff"
                        background: Rectangle {
                            color: "#333333"
                            radius: 4
                            border.color: confirmPasswordField.activeFocus ? "#4FC3F7" : "#444444"
                            border.width: confirmPasswordField.activeFocus ? 2 : 1
                        }
                    }

                    Text {
                        text: qsTr("⚠ 忘记密码将无法恢复 (Password cannot be recovered if lost)")
                        font.pixelSize: 10
                        font.family: "IBM Plex Sans"
                        color: "#FF8A65"
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }

                RadioButton {
                    id: autoKeyRadio
                    text: qsTr("自动生成密钥 (Auto-generate Key)")
                    Layout.fillWidth: true
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    contentItem: Text {
                        text: autoKeyRadio.text
                        font: autoKeyRadio.font
                        color: "#cccccc"
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: autoKeyRadio.indicator.width + root.space1
                    }
                }

                Text {
                    Layout.leftMargin: root.space3
                    Layout.fillWidth: true
                    text: qsTr("密钥将在加密完成后显示，请妥善保存 (Key will be shown after encryption, keep it safe)")
                    font.pixelSize: 10
                    font.family: "IBM Plex Sans"
                    color: "#FFB74D"
                    visible: autoKeyRadio.checked
                    wrapMode: Text.Wrap
                }
            }

            // Progress section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: root.space1
                visible: encryptionBridge.isProcessing

                Text {
                    text: qsTr("加密中 (Encrypting)...") + " " + Math.round(encryptionBridge.progress * 100) + "%"
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    color: "#ffffff"
                }

                ProgressBar {
                    Layout.fillWidth: true
                    value: encryptionBridge.progress
                    background: Rectangle {
                        color: "#444444"
                        radius: 2
                    }
                    contentItem: Rectangle {
                        width: parent.width * encryptionBridge.progress
                        height: parent.height
                        radius: 2
                        color: "#4FC3F7"
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: root.space1

                Button {
                    text: qsTr("取消 (Cancel)")
                    Layout.preferredWidth: 100
                    enabled: !encryptionBridge.isProcessing
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    background: Rectangle {
                        color: cancelButton.hovered ? "#444444" : "#333333"
                        radius: 4
                    }
                    contentItem: Text {
                        text: cancelButton.text
                        font: cancelButton.font
                        color: cancelButton.enabled ? "#cccccc" : "#666666"
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
                    text: qsTr("导出 (Export)")
                    Layout.preferredWidth: 100
                    enabled: encryptCheckbox.checked && !encryptionBridge.isProcessing
                    font.pixelSize: 12
                    font.family: "IBM Plex Sans"
                    background: Rectangle {
                        color: exportButton.enabled ? (exportButton.hovered ? "#3DB5D9" : "#4FC3F7") : "#333333"
                        radius: 4
                    }
                    contentItem: Text {
                        text: exportButton.text
                        font: exportButton.font
                        color: exportButton.enabled ? "#ffffff" : "#666666"
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