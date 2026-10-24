import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: panel
    objectName: "preferencesPanel"
    title: qsTranslate("Luma", "Settings")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 440)
    height: Math.min(parent.height - 60, 650)
    standardButtons: Dialog.Close
    CatalogMaintenanceDialog { id: catalogMaintenance; parent: Overlay.overlay }
    DiagnosticsDialog { id: diagnosticDialog; parent: Overlay.overlay }
    FileDialog {
        id: denoiseModelDialog
        title: qsTranslate("Luma", "Choose local denoise model")
        nameFilters: ["Core ML (*.mlmodelc)"]
        onAccepted: editor.setDenoiseModel(selectedFile)
    }
    FileDialog {
        id: skyModelDialog
        title: qsTranslate("Luma", "Choose local Sky model")
        nameFilters: ["Core ML (*.mlmodelc)"]
        onAccepted: editor.setSkyModel(selectedFile)
    }
    contentItem: ScrollView {
      clip: true; contentWidth: availableWidth
      ColumnLayout {
        width: parent.width
        spacing: 14
        Label { objectName: "settingsVersion"; text: "Luma " + Qt.application.version; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
        Label { objectName: "settingsLanguageLabel"; text: qsTranslate("Luma", "Interface language"); Layout.fillWidth: true; wrapMode: Text.Wrap }
        ComboBox {
            id: languageChoice
            objectName: "languageChoice"
            Layout.fillWidth: true
            model: localization.languages
            textRole: "name"
            valueRole: "code"
            currentIndex: {
                for (let i = 0; i < count; ++i)
                    if (model[i].code === localization.language) return i
                return 0
            }
            onActivated: localization.selectLanguage(model[currentIndex].code)
            Accessible.name: qsTranslate("Luma", "Interface language")
        }
        Label { objectName: "settingsEffect"; text: qsTranslate("Luma", "Changes take effect immediately."); Layout.fillWidth: true; wrapMode: Text.Wrap }
        Button { objectName: "settings.catalogMaintenance"; text: qsTranslate("Luma", "Catalog and backups"); onClicked: catalogMaintenance.open(); Layout.fillWidth: true }
        Button { text: qsTranslate("Luma", "Diagnostics"); onClicked: diagnosticDialog.open(); Layout.fillWidth: true }
        Label { text: qsTranslate("Luma", "Preview cache (MiB)"); Layout.fillWidth: true }
        SpinBox {
            objectName: "settings.previewCache"
            Layout.fillWidth: true
            from: 64; to: 2048; stepSize: 64
            value: editor.previewCacheMiB
            editable: true
            onValueModified: editor.previewCacheMiB = value
            Accessible.name: qsTranslate("Luma", "Preview cache (MiB)")
        }
        Button {
            objectName: "settings.clearPreviewCache"
            text: qsTranslate("Luma", "Clear preview cache")
            onClicked: editor.clearPreviewCache()
        }
        CheckBox {
            objectName: "settings.metalPreview"
            text: qsTranslate("Luma", "Use Metal for photo previews")
            checked: editor.metalPreview
            onToggled: editor.metalPreview = checked
        }
        Label {
            text: qsTranslate("Luma", "Local AI Denoise model") + ": " +
                  (editor.denoiseModelConfigured ? editor.denoiseModelName : qsTranslate("Luma", "Not configured")) +
                  (editor.denoiseModelBundled ? " · " + qsTranslate("Luma", "Included with Luma") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTranslate("Luma", "Choose model"); onClicked: denoiseModelDialog.open(); Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Use bundled model"); enabled: !editor.denoiseModelBundled; onClicked: editor.setDenoiseModel(""); Layout.fillWidth: true }
        }
        Label { text: qsTranslate("Luma", localization.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label {
            text: qsTranslate("Luma", "Local Sky model") + ": " + (editor.skyModelConfigured ? editor.skyModelName : qsTranslate("Luma", "Not configured")) +
                  (editor.skyModelBundled ? " · " + qsTranslate("Luma", "Included with Luma") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTranslate("Luma", "Choose model"); onClicked: skyModelDialog.open(); Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Use bundled model"); enabled: !editor.skyModelBundled; onClicked: editor.setSkyModel(""); Layout.fillWidth: true }
        }
        Label { text: qsTranslate("Luma", "Included models work offline. Photos stay on this Mac."); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
      }
    }
}
