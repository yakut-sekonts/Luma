import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Dialog {
    id: panel
    objectName: "preferencesPanel"
    title: qsTranslate("Apertone", "Settings")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 440)
    height: Math.min(parent.height - 60, 650)
    standardButtons: Dialog.Close
    CatalogMaintenanceDialog { id: catalogMaintenance; parent: Overlay.overlay }
    DiagnosticsDialog { id: diagnosticDialog; parent: Overlay.overlay }
    FileDialog {
        id: denoiseModelDialog
        title: qsTranslate("Apertone", "Choose local denoise model")
        nameFilters: ["Core ML (*.mlmodelc)"]
        onAccepted: editor.setDenoiseModel(selectedFile)
    }
    FileDialog {
        id: skyModelDialog
        title: qsTranslate("Apertone", "Choose local Sky model")
        nameFilters: ["Core ML (*.mlmodelc)"]
        onAccepted: editor.setSkyModel(selectedFile)
    }
    contentItem: ScrollView {
      clip: true; contentWidth: availableWidth
      ColumnLayout {
        width: parent.width
        spacing: 10
        Label { objectName: "settingsVersion"; text: "Apertone " + Qt.application.version; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
        Label { text: qsTranslate("Apertone", "Appearance"); Layout.fillWidth: true }
        ApertoneComboBox {
            id: appearanceChoice
            objectName: "appearanceChoice"
            Layout.fillWidth: true
            model: [qsTranslate("Apertone", "Follow system"), qsTranslate("Apertone", "Day"), qsTranslate("Apertone", "Night")]
            currentIndex: appearance.mode === "day" ? 1 : appearance.mode === "night" ? 2 : 0
            onActivated: appearance.selectMode(["system", "day", "night"][currentIndex])
            Accessible.name: qsTranslate("Apertone", "Appearance")
        }
        Label { text: qsTranslate("Apertone", appearance.error); textFormat: Text.PlainText; visible: text.length > 0; color: Theme.negative; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label { objectName: "settingsLanguageLabel"; text: qsTranslate("Apertone", "Interface language"); Layout.fillWidth: true; wrapMode: Text.Wrap }
        ApertoneComboBox {
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
            Accessible.name: qsTranslate("Apertone", "Interface language")
        }
        Label { objectName: "settingsEffect"; text: qsTranslate("Apertone", "Changes take effect immediately."); Layout.fillWidth: true; wrapMode: Text.Wrap }
        ApertoneButton { objectName: "settings.catalogMaintenance"; text: qsTranslate("Apertone", "Catalog and backups"); onClicked: catalogMaintenance.open(); Layout.fillWidth: true }
        ApertoneButton { text: qsTranslate("Apertone", "Diagnostics"); onClicked: diagnosticDialog.open(); Layout.fillWidth: true }
        Label { text: qsTranslate("Apertone", "Preview cache (MiB)"); Layout.fillWidth: true }
        ApertoneSpinBox {
            objectName: "settings.previewCache"
            Layout.fillWidth: true
            from: 64; to: 2048; stepSize: 64
            value: editor.previewCacheMiB
            editable: true
            onValueModified: editor.previewCacheMiB = value
            Accessible.name: qsTranslate("Apertone", "Preview cache (MiB)")
        }
        ApertoneButton {
            objectName: "settings.clearPreviewCache"
            text: qsTranslate("Apertone", "Clear preview cache")
            onClicked: editor.clearPreviewCache()
        }
        CheckBox {
            objectName: "settings.metalPreview"
            text: qsTranslate("Apertone", "Use Metal for photo previews")
            checked: editor.metalPreview
            onToggled: editor.metalPreview = checked
        }
        CheckBox {
            objectName: "settings.hdrToneMapping"
            text: qsTranslate("Apertone", "Map HDR highlights into the preview")
            checked: editor.hdrToneMapping
            onToggled: editor.hdrToneMapping = checked
        }
        Label {
            text: qsTranslate("Apertone", "Preview-only tone mapping preserves detail above SDR white. Histogram, clipping warnings and exports remain scene-linear.")
            wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11
        }
        Label {
            text: qsTranslate("Apertone", "Local AI Denoise model") + ": " +
                  (editor.denoiseModelConfigured ? editor.denoiseModelName : qsTranslate("Apertone", "Not configured")) +
                  (editor.denoiseModelBundled ? " · " + qsTranslate("Apertone", "Included with Apertone") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            ApertoneButton { text: qsTranslate("Apertone", "Choose model"); onClicked: denoiseModelDialog.open(); Layout.fillWidth: true }
            ApertoneButton { text: qsTranslate("Apertone", "Use bundled model"); enabled: !editor.denoiseModelBundled; onClicked: editor.setDenoiseModel(""); Layout.fillWidth: true }
        }
        Label { text: qsTranslate("Apertone", localization.error); textFormat: Text.PlainText; visible: text.length > 0; color: Theme.negative; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Label {
            text: qsTranslate("Apertone", "Local Sky model") + ": " + (editor.skyModelConfigured ? editor.skyModelName : qsTranslate("Apertone", "Not configured")) +
                  (editor.skyModelBundled ? " · " + qsTranslate("Apertone", "Included with Apertone") : "")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            ApertoneButton { text: qsTranslate("Apertone", "Choose model"); onClicked: skyModelDialog.open(); Layout.fillWidth: true }
            ApertoneButton { text: qsTranslate("Apertone", "Use bundled model"); enabled: !editor.skyModelBundled; onClicked: editor.setSkyModel(""); Layout.fillWidth: true }
        }
        Label { text: qsTranslate("Apertone", "Included models work offline. Photos stay on this Mac."); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
      }
    }
}
