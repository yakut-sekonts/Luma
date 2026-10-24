import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "catalogMaintenance"
    title: qsTranslate("Luma", "Catalog and backups")
    modal: true; anchors.centerIn: parent
    width: Math.min(parent.width - 40, 560)
    height: Math.min(parent.height - 40, 690)
    standardButtons: Dialog.Close
    property url restoreSource: ""
    FileDialog {
        id: newCatalog
        title: qsTranslate("Luma", "Create catalog")
        fileMode: FileDialog.SaveFile; defaultSuffix: "sqlite3"
        onAccepted: library.openCatalog(selectedFile, true)
    }
    FileDialog {
        id: openCatalog
        title: qsTranslate("Luma", "Open catalog in a new window")
        nameFilters: ["Luma catalog (*.sqlite3 *.sqlite *.db)"]
        onAccepted: library.openCatalog(selectedFile, false)
    }
    FileDialog {
        id: backupFile
        title: qsTranslate("Luma", "Choose catalog backup")
        nameFilters: ["Luma catalog (*.sqlite3 *.sqlite *.db)"]
        onAccepted: { dialog.restoreSource = selectedFile; restoredFile.open() }
    }
    FileDialog {
        id: restoredFile
        objectName: "catalog.restoreDestination"
        title: qsTranslate("Luma", "Restore to a new catalog file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "sqlite3"
        onAccepted: library.restoreCatalog(dialog.restoreSource, selectedFile)
    }
    contentItem: ScrollView {
      clip: true; contentWidth: availableWidth
      ColumnLayout {
        width: parent.width; spacing: 10
        Label { text: editor.catalogPath; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTranslate("Luma", "Create catalog"); enabled: !library.working; onClicked: newCatalog.open(); Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Open catalog in a new window"); enabled: !library.working; onClicked: openCatalog.open(); Layout.fillWidth: true }
        }
        Label { text: qsTranslate("Luma", "Automatic catalog backup") }
        ComboBox {
            model: [qsTranslate("Luma", "Manually"), qsTranslate("Luma", "Every launch"), qsTranslate("Luma", "Daily"), qsTranslate("Luma", "Weekly")]
            currentIndex: library.backupSchedule; onActivated: library.backupSchedule = currentIndex
            Layout.fillWidth: true; Accessible.name: qsTranslate("Luma", "Automatic catalog backup")
        }
        Label { text: qsTranslate("Luma", "Backups to keep") }
        SpinBox { from: 1; to: 50; value: library.backupKeep; onValueModified: library.backupKeep = value; Accessible.name: qsTranslate("Luma", "Backups to keep") }
        Label { text: qsTranslate("Luma", "Catalog backups contain edits and metadata. Keep a separate backup of your originals and generated DNG files."); wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Button { objectName: "catalog.backupNow"; text: qsTranslate("Luma", "Back up now"); enabled: !library.working && !editor.saving; onClicked: { editor.endEdit(); library.backupNow() } Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Show backups"); onClicked: Qt.openUrlExternally(library.backupFolder); Layout.fillWidth: true }
        }
        Button { text: qsTranslate("Luma", "Restore backup to a new file"); enabled: !library.working; onClicked: backupFile.open(); Layout.fillWidth: true }
        CheckBox { text: qsTranslate("Luma", "Automatically write metadata to XMP sidecar"); checked: library.autoWriteXMP; enabled: !library.working; onToggled: library.autoWriteXMP = checked; Layout.fillWidth: true }
        Button { text: qsTranslate("Luma", "Write pending XMP sidecars"); enabled: !library.working; onClicked: library.syncSidecars(); Layout.fillWidth: true }
        BusyIndicator { running: library.working; visible: running; Layout.alignment: Qt.AlignHCenter }
        Label { objectName: "catalog.status"; text: library.maintenanceStatus || library.status; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        Label { objectName: "catalog.notice"; text: library.maintenanceStatus && library.status !== library.maintenanceStatus ? library.status : ""; visible: text.length > 0; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
        Label { objectName: "catalog.error"; text: qsTranslate("Luma", library.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
      }
    }
}
