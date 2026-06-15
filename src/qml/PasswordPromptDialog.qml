import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import FluentUI

FluPopup {
    id: control
    property int keyMode: 1  // 1=password, 2=raw key
    property string inputText: ""
    property bool showError: false

    implicitWidth: 480
    implicitHeight: layout_content.height
    modal: true
    dim: true
    focus: true

    function clearAndFocus() {
        inputText = ""
        showError = false
        txt_input.text = ""
        txt_input.forceActiveFocus()
    }

    Rectangle {
        id: layout_content
        width: parent.width
        height: layout_column.childrenRect.height
        color: FluTheme.dark ? Qt.rgba(32/255, 32/255, 32/255, 1) : Qt.rgba(255/255, 255/255, 255/255, 1)
        radius: 8

        ColumnLayout {
            id: layout_column
            width: parent.width
            spacing: 16

            FluText {
                text: keyMode === 1 ? "输入密码 / Enter Password" : "输入密钥 / Enter Key"
                font: FluTextStyle.Subtitle
                Layout.topMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
            }

            Item {
                Layout.preferredHeight: 1
            }

            FluTextBox {
                id: txt_input
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                placeholderText: keyMode === 1 ? "请输入密码 / Enter password" : "请输入密钥 (格式: XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX) / Enter key"
                echoMode: keyMode === 1 ? TextField.Password : TextField.Normal
                selectByMouse: true
                focus: true

                onTextChanged: {
                    inputText = text
                    if (showError) {
                        showError = false
                    }
                }

                Keys.onEnterPressed: (event) => btn_ok.clicked()
                Keys.onReturnPressed: (event) => btn_ok.clicked()

                Component.onCompleted: {
                    forceActiveFocus()
                }
            }

            FluText {
                text: "密码/密钥错误 / Wrong password or key"
                font: FluTextStyle.Caption
                color: "#FF4500"
                visible: showError
                Layout.leftMargin: 20
                Layout.rightMargin: 20
            }

            Item {
                Layout.preferredHeight: 8
            }

            Rectangle {
                id: layout_actions
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                Layout.bottomMargin: 12
                color: "transparent"
                radius: 5

                RowLayout {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    spacing: 12

                    FluButton {
                        id: btn_cancel
                        text: "取消 / Cancel"
                        Layout.fillWidth: true
                        onClicked: {
                            inputText = ""
                            showError = false
                            control.close()
                        }
                    }

                    FluFilledButton {
                        id: btn_ok
                        text: "确定 / OK"
                        Layout.fillWidth: true
                        enabled: txt_input.text.length > 0
                        onClicked: {
                            inputText = txt_input.text
                            control.close()
                        }
                    }
                }
            }
        }
    }

    onOpened: {
        clearAndFocus()
    }
}