import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HLPlayer

ApplicationWindow {
    id: root
    width: 1280
    height: 720
    visible: true
    title: "Offline VSR Studio"
    color: "#1a1a1a"

    OfflineVSRBridge { id: bridge }

    FileDialog {
        id: importDialog
        title: "Import Video"
        nameFilters: ["Video files (*.mp4 *.mkv *.avi *.mov)", "All files (*)"]
        onAccepted: {
                console.log("VSR: File selected =", selectedFile)
                console.log("VSR: bridge object =", bridge)
                bridge.importVideo(selectedFile)
            }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Row {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            spacing: 8
            padding: 8

            Button { text: "Import"; onClicked: { console.log("VSR: Import clicked, opening dialog..."); importDialog.open() } }
            Button { text: "Start"; enabled: bridge.state === 1; onClicked: bridge.startProcessing() }
            Button { text: "Pause"; enabled: bridge.state === 2; onClicked: bridge.pauseProcessing() }
            Button { text: "Resume"; enabled: bridge.state === 3; onClicked: bridge.resumeProcessing() }
            Button { text: "Cancel"; enabled: bridge.state === 2 || bridge.state === 3; onClicked: bridge.cancelProcessing() }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            VSRParameterPanel { bridge: bridge; Layout.preferredWidth: 280; Layout.fillHeight: true }
            VSRPreviewArea { bridge: bridge; Layout.fillWidth: true; Layout.fillHeight: true }
        }

        VSRStatusBar { bridge: bridge; Layout.fillWidth: true; Layout.preferredHeight: 32 }
    }
}
