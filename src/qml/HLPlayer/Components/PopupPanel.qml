import QtQuick
import QtQuick.Controls
import HLPlayer

Popup {
    id: root

    property int panelWidth: 160
    property int itemHeight: 32
    property var model: []
    property var currentValue: undefined
    property var onItemSelected: function(value) {}

    width: panelWidth
    padding: 6
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: ThemeManager.bgSidebar
        radius: ThemeManager.isDarkMode ? 10 : 6
        border.color: ThemeManager.border
        border.width: 0.5

        layer.enabled: ThemeManager.isDarkMode
        layer.effect: DropShadow {
            radius: 16
            samples: 25
            color: "#80000000"
        }
    }

    contentItem: ListView {
        implicitHeight: Math.min(contentHeight, 400)
        model: root.model
        spacing: 2
        clip: true
        ScrollBar.vertical: ScrollBar {}

        delegate: ItemDelegate {
            width: root.panelWidth - 12
            height: root.itemHeight

            readonly property var itemData: typeof modelData !== "undefined" ? modelData : model
            readonly property bool isCurrent: {
                if (root.currentValue === undefined) return false
                var val = itemData.value !== undefined ? itemData.value : itemData
                return Math.abs(val - root.currentValue) < 0.005
            }

            background: Rectangle {
                radius: ThemeManager.isDarkMode ? 6 : 4
                color: parent.hovered
                       ? ThemeManager.hoverHighlight
                       : (parent.isCurrent ? ThemeManager.accentColor : "transparent")
                opacity: parent.isCurrent ? 0.20 : 1.0
            }

            contentItem: Text {
                text: itemData.label !== undefined ? itemData.label : String(itemData)
                color: parent.isCurrent ? ThemeManager.accentColor : ThemeManager.textPrimary
                font.pixelSize: 13
                font.bold: parent.isCurrent
                verticalAlignment: Text.AlignVCenter
                leftPadding: 12
            }

            onClicked: {
                var val = itemData.value !== undefined ? itemData.value : itemData
                root.onItemSelected(val)
                root.close()
            }
        }
    }
}
