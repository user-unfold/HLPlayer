import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    // Public properties to bind to QMLASRBridge
    property string subtitleText: ""
    property string translatedText: ""
    property int displayMode: 0  // 0=OriginalOnly, 1=TranslationOnly, 2=Bilingual
    property int fontSize: 18
    property color fontColor: "#FFFFFF"

    // Transparent background — text renders directly on top of video
    color: "transparent"

    // Auto-size height with max limit
    implicitHeight: contentColumn.implicitHeight
    height: Math.min(implicitHeight, 100)

    // Fade animation for smooth transitions
    Behavior on opacity {
        NumberAnimation { duration: 200 }
    }

    Column {
        id: contentColumn
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 4

        // Original subtitle text (top row)
        // Visible in OriginalOnly or Bilingual mode
        Text {
            id: originalText
            width: parent.width
            text: root.subtitleText
            font.family: "Microsoft YaHei, SimHei, sans-serif"
            font.pixelSize: root.fontSize
            color: root.fontColor
            style: Text.Outline
            styleColor: "#80000000"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            visible: root.displayMode === 0 || root.displayMode === 2

            // Fade animation when content changes
            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }

        // Translated subtitle text (bottom row)
        // Visible in TranslationOnly or Bilingual mode
        Text {
            id: translatedText
            width: parent.width
            text: root.translatedText
            font.family: "Microsoft YaHei, SimHei, sans-serif"
            font.pixelSize: root.fontSize
            color: root.fontColor
            style: Text.Outline
            styleColor: "#80000000"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            visible: root.displayMode === 1 || root.displayMode === 2

            // Fade animation when content changes
            Behavior on opacity {
                NumberAnimation { duration: 150 }
            }
        }
    }
}