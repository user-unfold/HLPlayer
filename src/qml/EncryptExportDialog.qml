import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

Dialog {
    id: root
    width: 480
    height: 520
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose

    property string inputPath: ""
    property string outputPath: ""
    property bool usePasswordMode: true

    readonly property int space1: 8

    function deriveOutputPath(input) {
        if (!input) return ""
        var dotPos = input.lastIndexOf(".")
        if (dotPos > 0)
            return input.substring(0, dotPos) + ".hlv"
        return input + ".hlv"
    }

    onInputPathChanged: {
        outputPath = deriveOutputPath(inputPath)
    }

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
            spacing: 12

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

            CheckBox {
                id: encryptCheckbox
                text: qsTr("加密导出")
                checked: true
                Layout.fillWidth: true
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

            // Input / Output paths
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                visible: encryptCheckbox.checked

                Text {
                    text: qsTr("输入文件")
                    font.pixelSize: 11
                    color: "#888888"
                    font.family: "IBM Plex Sans"
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        var n = root.inputPath
                        if (n.length > 50) n = "..." + n.substring(n.length - 47)
                        return n || qsTr("(未选择)")
                    }
                    font.pixelSize: 11
                    color: "#cccccc"
                    font.family: "IBM Plex Sans"
                    elide: Text.ElideRight
                }

                Text {
                    text: qsTr("输出位置")
                    font.pixelSize: 11
                    color: "#888888"
                    font.family: "IBM Plex Sans"
                    Layout.topMargin: 4
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    TextField {
                        id: outputPathField
                        Layout.fillWidth: true
                        implicitHeight: 32
                        text: root.outputPath
                        font.pixelSize: 11
                        font.family: "IBM Plex Sans"
                        color: "#ffffff"
                        onTextChanged: root.outputPath = text
                        background: Rectangle {
                            color: "#222233"
                            radius: 6
                            border.color: outputPathField.activeFocus ? "#4FC3F7" : "#444455"
                            border.width: outputPathField.activeFocus ? 2 : 1
                        }
                    }

                    Button {
                        Layout.preferredHeight: 32
                        text: qsTr("浏览...")
                        font.pixelSize: 11
                        font.family: "IBM Plex Sans"
                        background: Rectangle {
                            color: parent.hovered ? "#3a3a4e" : "#2a2a3e"
                            radius: 6
                            border.color: "#444466"
                            border.width: 1
                        }
                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            color: "#cccccc"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        onClicked: outputSaveDialog.open()
                    }
                }
            }

            // Key mode cards
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: encryptCheckbox.checked

                // Password mode card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: 8
                    color: root.usePasswordMode ? "#1e3a5f" : "#252535"
                    border.color: root.usePasswordMode ? "#4FC3F7" : "#333344"
                    border.width: root.usePasswordMode ? 2 : 1

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.usePasswordMode = true
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            width: 18; height: 18
                            radius: 9
                            Layout.alignment: Qt.AlignVCenter
                            color: "transparent"
                            border.color: root.usePasswordMode ? "#4FC3F7" : "#666666"
                            border.width: 2

                            Rectangle {
                                anchors.centerIn: parent
                                width: 8; height: 8
                                radius: 4
                                color: "#4FC3F7"
                                visible: root.usePasswordMode
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("使用密码 — 输入密码加密，播放时需输入相同密码")
                            font.pixelSize: 12
                            font.family: "IBM Plex Sans"
                            color: root.usePasswordMode ? "#ffffff" : "#aaaaaa"
                            wrapMode: Text.Wrap
                        }
                    }
                }

                // Password fields
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 4
                    visible: root.usePasswordMode

                    TextField {
                        id: passwordField
                        Layout.fillWidth: true
                        implicitHeight: 32
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
                        implicitHeight: 32
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
                        color: "#FF8A65"
                        font.family: "IBM Plex Sans"
                    }
                }

                // Auto-key mode card
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    radius: 8
                    color: !root.usePasswordMode ? "#1e3a5f" : "#252535"
                    border.color: !root.usePasswordMode ? "#4FC3F7" : "#333344"
                    border.width: !root.usePasswordMode ? 2 : 1

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.usePasswordMode = false
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        Rectangle {
                            width: 18; height: 18
                            radius: 9
                            Layout.alignment: Qt.AlignVCenter
                            color: "transparent"
                            border.color: !root.usePasswordMode ? "#4FC3F7" : "#666666"
                            border.width: 2

                            Rectangle {
                                anchors.centerIn: parent
                                width: 8; height: 8
                                radius: 4
                                color: "#4FC3F7"
                                visible: !root.usePasswordMode
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("自动生成密钥 — 系统生成密钥，加密后显示，请妥善保存")
                            font.pixelSize: 12
                            font.family: "IBM Plex Sans"
                            color: !root.usePasswordMode ? "#ffffff" : "#aaaaaa"
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // Progress section
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: encryptionBridge.isProcessing

                Text {
                    text: qsTr("加密中...") + " " + Math.round(encryptionBridge.progress * 100) + "%"
                    font.pixelSize: 12
                    color: "#ffffff"
                    font.family: "IBM Plex Sans"
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

            // Error message
            Text {
                Layout.fillWidth: true
                property string errMsg: ""
                text: errMsg
                visible: errMsg.length > 0
                font.pixelSize: 11
                color: "#FF5252"
                font.family: "IBM Plex Sans"
                wrapMode: Text.Wrap
            }

            Item { Layout.fillHeight: true }

            // Buttons
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    id: cancelButton
                    text: qsTr("取消")
                    Layout.preferredWidth: 100
                    Layout.preferredHeight: 36
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
                    onClicked: root.close()
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: exportButton
                    text: qsTr("导出")
                    Layout.preferredWidth: 120
                    Layout.preferredHeight: 36
                    enabled: encryptCheckbox.checked
                             && !encryptionBridge.isProcessing
                             && root.inputPath.length > 0
                             && root.outputPath.length > 0
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

                        if (root.usePasswordMode) {
                            var password = passwordField.text.trim()
                            var confirmPassword = confirmPasswordField.text.trim()

                            if (password.length < 8) {
                                console.warn("Password too short")
                                return
                            }

                            if (password !== confirmPassword) {
                                console.warn("Passwords do not match")
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

    FileDialog {
        id: outputSaveDialog
        title: qsTr("选择保存位置")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "hlv"
        nameFilters: ["HLV encrypted video (*.hlv)"]
        onAccepted: {
            var path = selectedFile.toString()
            if (path.startsWith("file:///"))
                path = decodeURIComponent(path.substring(8).replace(/\\/g, "/"))
            root.outputPath = path
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
            console.warn("Encryption failed:", error)
        }
    }
}
