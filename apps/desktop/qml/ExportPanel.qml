import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: panel
    objectName: "exportPanel"
    title: qsTranslate("Apertone", "Export")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 650)
    height: Math.min(parent.height - 50, 790)
    property var form: exporter.settings
    function update(key, value) { const copy = Object.assign({}, form); copy[key] = value; form = copy }
    onOpened: { form = exporter.settings; presets.clearPreview() }
    Connections { target: exporter; function onSettingsChanged() { panel.form = exporter.settings } }
    FolderDialog { id: folder; title: qsTranslate("Apertone", "Export folder"); onAccepted: panel.update("destination", exporter.localPath(selectedFolder)) }
    FileDialog { id: watermarkFile; title: qsTranslate("Apertone", "Watermark image"); nameFilters: [qsTranslate("Apertone", "Images (*.png *.jpg *.jpeg *.tif *.tiff)")]; onAccepted: panel.update("watermarkImage", exporter.localPath(selectedFile)) }
    contentItem: ScrollView {
        objectName: "export.scroll"
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 10
            enabled: !exporter.working
            RowLayout {
                Layout.fillWidth: true
                ApertoneComboBox { id: profiles; objectName: "export.profiles"; Layout.fillWidth: true; model: exporter.profiles; textRole: "name"; valueRole: "id"; displayText: count ? currentText : qsTranslate("Apertone", "Saved settings") }
                ApertoneButton { objectName: "export.profileLoad"; text: qsTranslate("Apertone", "Load"); enabled: profiles.count > 0; onClicked: exporter.loadProfile(profiles.currentValue) }
                ApertoneButton { text: qsTranslate("Apertone", "Delete"); enabled: profiles.count > 0; onClicked: exporter.deleteProfile(profiles.currentValue) }
            }
            RowLayout {
                ApertoneTextField { id: profileName; objectName: "export.profileName"; placeholderText: qsTranslate("Apertone", "Settings name"); maximumLength: 80; Layout.fillWidth: true }
                ApertoneButton { objectName: "export.profileSave"; text: qsTranslate("Apertone", "Save"); onClicked: { if (exporter.configure(panel.form)) exporter.saveProfile(profileName.text) } }
            }
            Label { text: qsTranslate("Apertone", "Destination folder") }
            RowLayout {
                ApertoneTextField { objectName: "export.destination"; text: panel.form.destination; Layout.fillWidth: true; onEditingFinished: panel.update("destination", text) }
                ApertoneButton { text: qsTranslate("Apertone", "Choose…"); onClicked: folder.open() }
            }
            Label { text: qsTranslate("Apertone", "Filename · {filename}, {sequence}, {date}"); font.pixelSize: 12 }
            ApertoneTextField { objectName: "exportNaming"; text: panel.form.naming; Layout.fillWidth: true; onEditingFinished: panel.update("naming", text) }
            ApertoneComboBox { objectName: "export.conflict"; model: [qsTranslate("Apertone", "Existing files: skip"), qsTranslate("Apertone", "Existing files: unique name"), qsTranslate("Apertone", "Existing files: replace")]; currentIndex: panel.form.conflict; onActivated: panel.update("conflict", currentIndex); Layout.fillWidth: true }
            Label { visible: panel.form.conflict === 2; text: qsTranslate("Apertone", "Files with matching names will be replaced."); color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ApertoneComboBox { objectName: "exportFormat"; model: ["JPEG", "PNG", "TIFF · 16 bit", "TIFF · 8 bit", qsTranslate("Apertone", "DNG · processed Linear RGB"), qsTranslate("Apertone", "HEIC · Adaptive HDR")]; currentIndex: panel.form.format; onActivated: { panel.update("format", currentIndex); if (currentIndex !== 2 && currentIndex !== 3) panel.update("compression", 0); if (currentIndex === 5) panel.update("colorSpace", 0) } Layout.fillWidth: true }
            Label { visible: panel.form.format === 4; text: qsTranslate("Apertone", "DNG contains applied edits, not archived camera sensor data. Minimum 22 px on each side."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
            Label { visible: panel.form.format === 5; text: qsTranslate("Apertone", "Adaptive HDR includes an SDR-compatible base and ISO gain map. Requires macOS 15 or later."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
            RowLayout {
                visible: panel.form.format === 0 || panel.form.format === 5
                Label { text: panel.form.format === 5 ? qsTranslate("Apertone", "HEIC quality") : qsTranslate("Apertone", "JPEG quality") }
                ApertoneSpinBox { from: 1; to: 100; value: panel.form.quality; editable: true; onValueModified: panel.update("quality", value) }
            }
            ApertoneComboBox { objectName: "export.compression"; visible: panel.form.format === 2 || panel.form.format === 3; model: [qsTranslate("Apertone", "TIFF · uncompressed"), "TIFF · LZW", "TIFF · ZIP"]; currentIndex: panel.form.compression; onActivated: panel.update("compression", currentIndex); Layout.fillWidth: true }
            ApertoneComboBox { objectName: "export.colorSpace"; enabled: panel.form.format !== 5; model: ["sRGB", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]; currentIndex: panel.form.colorSpace; onActivated: panel.update("colorSpace", currentIndex); Layout.fillWidth: true }
            Label { text: qsTranslate("Apertone", "Size after Crop / Transform") }
            RowLayout {
                ApertoneComboBox { objectName: "export.resize"; model: [qsTranslate("Apertone", "Original size"), qsTranslate("Apertone", "Width, px"), qsTranslate("Apertone", "Height, px"), qsTranslate("Apertone", "Long edge, px"), qsTranslate("Apertone", "Short edge, px"), qsTranslate("Apertone", "Megapixels"), qsTranslate("Apertone", "Percentage")]; currentIndex: panel.form.resize; onActivated: panel.update("resize", currentIndex); Layout.fillWidth: true }
                ApertoneTextField { objectName: "export.resizeValue"; visible: panel.form.resize !== 0; text: panel.form.resizeValue; Layout.preferredWidth: 100; validator: DoubleValidator { bottom: 0.01; top: 20000; locale: "C" } onEditingFinished: panel.update("resizeValue", Number(text)) }
            }
            Label { text: qsTranslate("Apertone", "Output sharpening") }
            RowLayout {
                ApertoneComboBox { objectName: "export.outputMedium"; model: [qsTranslate("Apertone", "Off"), qsTranslate("Apertone", "Screen"), qsTranslate("Apertone", "Matte paper"), qsTranslate("Apertone", "Glossy paper")]; currentIndex: panel.form.outputMedium; onActivated: panel.update("outputMedium", currentIndex); Layout.fillWidth: true }
                ApertoneComboBox { objectName: "export.outputAmount"; enabled: panel.form.outputMedium !== 0; model: [qsTranslate("Apertone", "Low"), qsTranslate("Apertone", "Standard"), qsTranslate("Apertone", "High")]; currentIndex: panel.form.outputAmount; onActivated: panel.update("outputAmount", currentIndex) }
            }
            Label { text: qsTranslate("Apertone", "Metadata") }
            ApertoneComboBox { objectName: "export.metadata"; model: [qsTranslate("Apertone", "Do not copy"), qsTranslate("Apertone", "All available photo metadata"), qsTranslate("Apertone", "Author / copyright only"), qsTranslate("Apertone", "Remove location")]; currentIndex: panel.form.metadata; onActivated: panel.update("metadata", currentIndex); Layout.fillWidth: true }
            Label { text: qsTranslate("Apertone", "MakerNotes, embedded previews and camera processing instructions are not copied."); font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Apertone", "Watermark") }
            ApertoneComboBox { objectName: "export.watermarkKind"; model: [qsTranslate("Apertone", "None"), qsTranslate("Apertone", "Text"), qsTranslate("Apertone", "Image")]; currentIndex: panel.form.watermarkKind; onActivated: panel.update("watermarkKind", currentIndex); Layout.fillWidth: true }
            ApertoneTextField { visible: panel.form.watermarkKind === 1; text: panel.form.watermarkText; placeholderText: qsTranslate("Apertone", "© Author"); maximumLength: 1024; onEditingFinished: panel.update("watermarkText", text); Layout.fillWidth: true }
            RowLayout {
                visible: panel.form.watermarkKind === 2
                ApertoneTextField { objectName: "export.watermarkImage"; text: panel.form.watermarkImage; onEditingFinished: panel.update("watermarkImage", text); Layout.fillWidth: true }
                ApertoneButton { text: qsTranslate("Apertone", "File…"); onClicked: watermarkFile.open() }
            }
            GridLayout {
                visible: panel.form.watermarkKind !== 0
                columns: 2; Layout.fillWidth: true
                Label { text: qsTranslate("Apertone", "Opacity, %") }
                ApertoneSpinBox { objectName: "export.watermarkOpacity"; from: 0; to: 100; value: panel.form.watermarkOpacity; editable: true; onValueModified: panel.update("watermarkOpacity", value) }
                Label { text: qsTranslate("Apertone", "Size, % of long edge") }
                ApertoneSpinBox { objectName: "export.watermarkSize"; from: 1; to: 100; value: panel.form.watermarkSize; editable: true; onValueModified: panel.update("watermarkSize", value) }
                Label { text: qsTranslate("Apertone", "Margin, % of short edge") }
                ApertoneSpinBox { objectName: "export.watermarkMargin"; from: 0; to: 25; value: panel.form.watermarkMargin; editable: true; onValueModified: panel.update("watermarkMargin", value) }
                Label { text: qsTranslate("Apertone", "Position") }
                ApertoneComboBox { objectName: "export.watermarkAnchor"; model: [qsTranslate("Apertone", "Top left"), qsTranslate("Apertone", "Top center"), qsTranslate("Apertone", "Top right"), qsTranslate("Apertone", "Left"), qsTranslate("Apertone", "Center"), qsTranslate("Apertone", "Right"), qsTranslate("Apertone", "Bottom left"), qsTranslate("Apertone", "Bottom center"), qsTranslate("Apertone", "Bottom right")]; currentIndex: panel.form.watermarkAnchor; onActivated: panel.update("watermarkAnchor", currentIndex) }
            }
        }
    }
    footer: ColumnLayout {
        Label { text: qsTranslate("Apertone", exporter.error); textFormat: Text.PlainText; visible: text.length > 0; color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            ApertoneButton { objectName: "export.close"; text: qsTranslate("Apertone", "Close"); onClicked: panel.close() }
            ApertoneButton { objectName: "export.current"; text: qsTranslate("Apertone", "Current photo"); enabled: editor.hasPhoto && !exporter.working; onClicked: { if (exporter.configure(panel.form)) { exporter.start(false); if (exporter.working) panel.close() } } }
            ApertoneButton { objectName: "export.selection"; text: qsTranslate("Apertone", "Selected photos"); enabled: library.selectedCount > 0 && !exporter.working; onClicked: { if (exporter.configure(panel.form)) { exporter.start(true); if (exporter.working) panel.close() } } }
        }
    }
}
