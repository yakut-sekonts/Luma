import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: window
    title: qsTranslate("Luma", "Open Luma catalog")
    visible: true
    width: 620; height: 570
    minimumWidth: 540; minimumHeight: 420
    color: "#202124"
    palette.windowText: "#eeeeee"
    onClosing: function(event) { event.accepted = false; startup.quit() }
    property url restoreSource: ""
    FileDialog {
        id: openFile
        title: qsTranslate("Luma", "Open catalog")
        nameFilters: ["Luma catalog (*.sqlite3 *.sqlite *.db)"]
        onAccepted: startup.openCatalog(selectedFile)
    }
    FileDialog {
        id: newFile
        title: qsTranslate("Luma", "Create catalog")
        fileMode: FileDialog.SaveFile; defaultSuffix: "sqlite3"
        onAccepted: startup.createCatalog(selectedFile)
    }
    FileDialog {
        id: backupFile
        title: qsTranslate("Luma", "Choose catalog backup")
        nameFilters: ["Luma catalog (*.sqlite3 *.sqlite *.db)"]
        currentFolder: startup.backupFolder
        onAccepted: { window.restoreSource = selectedFile; restoreFile.open() }
    }
    FileDialog {
        id: restoreFile
        title: qsTranslate("Luma", "Restore to a new catalog file")
        fileMode: FileDialog.SaveFile; defaultSuffix: "sqlite3"
        onAccepted: startup.restoreBackup(window.restoreSource, selectedFile)
    }
    ScrollView {
        anchors.fill: parent; anchors.margins: 24
        clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 14
            Label { text: "Luma"; font.pixelSize: 28; font.bold: true }
            Label {
                text: startup.busy
                    ? (startup.quitting ? qsTranslate("Luma", "Finishing catalog operation…") : qsTranslate("Luma", "Opening catalog…"))
                    : (startup.recoveryPending ? qsTranslate("Luma", "Recover previous session") : qsTranslate("Luma", "Cannot open this catalog"))
                font.pixelSize: 18; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label { text: startup.catalogPath; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            BusyIndicator { running: startup.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
            Label {
                visible: startup.recoveryPending
                text: qsTranslate("Luma", "The previous session ended unexpectedly. The catalog passed its integrity check. Reopen the last photo or continue in Library.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label {
                visible: startup.error.length > 0
                text: qsTranslate("Luma", startup.error); textFormat: Text.PlainText
                color: "#ffc1bc"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true
            }
            ColumnLayout {
                visible: startup.recoveryPending; enabled: !startup.busy && !startup.quitting
                Layout.fillWidth: true
                Button { text: qsTranslate("Luma", "Recover previous session"); onClicked: startup.continueSession(true); Layout.fillWidth: true }
                Button { text: qsTranslate("Luma", "Continue in Library"); onClicked: startup.continueSession(false); Layout.fillWidth: true }
            }
            ColumnLayout {
                visible: !startup.busy; enabled: !startup.quitting; Layout.fillWidth: true
                Button { text: qsTranslate("Luma", "Open catalog"); onClicked: openFile.open(); Layout.fillWidth: true }
                Button { text: qsTranslate("Luma", "Restore backup to a new file"); onClicked: backupFile.open(); Layout.fillWidth: true }
                Button { text: qsTranslate("Luma", "Create catalog"); onClicked: newFile.open(); Layout.fillWidth: true }
                Label {
                    text: qsTranslate("Luma", "Restoration creates a new catalog. The previous catalog and backup are kept. Originals and generated DNG files need their separate backups.")
                    wrapMode: Text.Wrap; Layout.fillWidth: true
                }
            }
            Button { text: qsTranslate("Luma", "Quit"); enabled: !startup.quitting; onClicked: startup.quit(); Layout.fillWidth: true }
        }
    }
}
