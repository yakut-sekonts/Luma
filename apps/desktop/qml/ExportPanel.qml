import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: panel
    objectName: "exportPanel"
    title: qsTranslate("Luma", "Export")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 60, 650)
    height: Math.min(parent.height - 50, 790)
    property var form: exporter.settings
    function update(key, value) { const copy = Object.assign({}, form); copy[key] = value; form = copy }
    onOpened: { form = exporter.settings; presets.clearPreview() }
    Connections { target: exporter; function onSettingsChanged() { panel.form = exporter.settings } }
    FolderDialog { id: folder; title: qsTranslate("Luma", "Export folder"); onAccepted: panel.update("destination", exporter.localPath(selectedFolder)) }
    FileDialog { id: watermarkFile; title: qsTranslate("Luma", "Watermark image"); nameFilters: [qsTranslate("Luma", "Images (*.png *.jpg *.jpeg *.tif *.tiff)")]; onAccepted: panel.update("watermarkImage", exporter.localPath(selectedFile)) }
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
                ComboBox { id: profiles; Layout.fillWidth: true; model: exporter.profiles; textRole: "name"; valueRole: "id"; displayText: count ? currentText : qsTranslate("Luma", "Saved settings") }
                Button { text: qsTranslate("Luma", "Load"); enabled: profiles.count > 0; onClicked: exporter.loadProfile(profiles.currentValue) }
                Button { text: qsTranslate("Luma", "Delete"); enabled: profiles.count > 0; onClicked: exporter.deleteProfile(profiles.currentValue) }
            }
            RowLayout {
                TextField { id: profileName; placeholderText: qsTranslate("Luma", "Settings name"); maximumLength: 80; Layout.fillWidth: true }
                Button { text: qsTranslate("Luma", "Save"); onClicked: { if (exporter.configure(panel.form)) exporter.saveProfile(profileName.text) } }
            }
            Label { text: qsTranslate("Luma", "Destination folder") }
            RowLayout {
                TextField { objectName: "export.destination"; text: panel.form.destination; Layout.fillWidth: true; onEditingFinished: panel.update("destination", text) }
                Button { text: qsTranslate("Luma", "Choose…"); onClicked: folder.open() }
            }
            Label { text: qsTranslate("Luma", "Filename · {filename}, {sequence}, {date}"); font.pixelSize: 12 }
            TextField { objectName: "exportNaming"; text: panel.form.naming; Layout.fillWidth: true; onEditingFinished: panel.update("naming", text) }
            ComboBox { objectName: "export.conflict"; model: [qsTranslate("Luma", "Existing files: skip"), qsTranslate("Luma", "Existing files: unique name"), qsTranslate("Luma", "Existing files: replace")]; currentIndex: panel.form.conflict; onActivated: panel.update("conflict", currentIndex); Layout.fillWidth: true }
            Label { visible: panel.form.conflict === 2; text: qsTranslate("Luma", "Files with matching names will be replaced."); color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ComboBox { objectName: "exportFormat"; model: ["JPEG", "PNG", "TIFF · 16 bit", "TIFF · 8 bit", qsTranslate("Luma", "DNG · processed Linear RGB")]; currentIndex: panel.form.format; onActivated: { panel.update("format", currentIndex); if (currentIndex !== 2 && currentIndex !== 3) panel.update("compression", 0) } Layout.fillWidth: true }
            Label { visible: panel.form.format === 4; text: qsTranslate("Luma", "DNG contains applied edits, not archived camera sensor data. Minimum 22 px on each side."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
            RowLayout {
                visible: panel.form.format === 0
                Label { text: qsTranslate("Luma", "JPEG quality") }
                SpinBox { from: 1; to: 100; value: panel.form.quality; editable: true; onValueModified: panel.update("quality", value) }
            }
            ComboBox { visible: panel.form.format === 2 || panel.form.format === 3; model: [qsTranslate("Luma", "TIFF · uncompressed"), "TIFF · LZW", "TIFF · ZIP"]; currentIndex: panel.form.compression; onActivated: panel.update("compression", currentIndex); Layout.fillWidth: true }
            ComboBox { model: ["sRGB", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]; currentIndex: panel.form.colorSpace; onActivated: panel.update("colorSpace", currentIndex); Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "Size after Crop / Transform") }
            RowLayout {
                ComboBox { model: [qsTranslate("Luma", "Original size"), qsTranslate("Luma", "Width, px"), qsTranslate("Luma", "Height, px"), qsTranslate("Luma", "Long edge, px"), qsTranslate("Luma", "Short edge, px"), qsTranslate("Luma", "Megapixels"), qsTranslate("Luma", "Percentage")]; currentIndex: panel.form.resize; onActivated: panel.update("resize", currentIndex); Layout.fillWidth: true }
                TextField { visible: panel.form.resize !== 0; text: panel.form.resizeValue; Layout.preferredWidth: 100; validator: DoubleValidator { bottom: 0.01; top: 20000; locale: "C" } onEditingFinished: panel.update("resizeValue", Number(text)) }
            }
            Label { text: qsTranslate("Luma", "Output sharpening") }
            RowLayout {
                ComboBox { model: [qsTranslate("Luma", "Off"), qsTranslate("Luma", "Screen"), qsTranslate("Luma", "Matte paper"), qsTranslate("Luma", "Glossy paper")]; currentIndex: panel.form.outputMedium; onActivated: panel.update("outputMedium", currentIndex); Layout.fillWidth: true }
                ComboBox { enabled: panel.form.outputMedium !== 0; model: [qsTranslate("Luma", "Low"), qsTranslate("Luma", "Standard"), qsTranslate("Luma", "High")]; currentIndex: panel.form.outputAmount; onActivated: panel.update("outputAmount", currentIndex) }
            }
            Label { text: qsTranslate("Luma", "Metadata") }
            ComboBox { model: [qsTranslate("Luma", "Do not copy"), qsTranslate("Luma", "All available photo metadata"), qsTranslate("Luma", "Author / copyright only"), qsTranslate("Luma", "Remove location")]; currentIndex: panel.form.metadata; onActivated: panel.update("metadata", currentIndex); Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "MakerNotes, embedded previews and camera processing instructions are not copied."); font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "Watermark") }
            ComboBox { model: [qsTranslate("Luma", "None"), qsTranslate("Luma", "Text"), qsTranslate("Luma", "Image")]; currentIndex: panel.form.watermarkKind; onActivated: panel.update("watermarkKind", currentIndex); Layout.fillWidth: true }
            TextField { visible: panel.form.watermarkKind === 1; text: panel.form.watermarkText; placeholderText: qsTranslate("Luma", "© Author"); maximumLength: 1024; onEditingFinished: panel.update("watermarkText", text); Layout.fillWidth: true }
            RowLayout {
                visible: panel.form.watermarkKind === 2
                TextField { text: panel.form.watermarkImage; onEditingFinished: panel.update("watermarkImage", text); Layout.fillWidth: true }
                Button { text: qsTranslate("Luma", "File…"); onClicked: watermarkFile.open() }
            }
            GridLayout {
                visible: panel.form.watermarkKind !== 0
                columns: 2; Layout.fillWidth: true
                Label { text: qsTranslate("Luma", "Opacity, %") }
                SpinBox { from: 0; to: 100; value: panel.form.watermarkOpacity; editable: true; onValueModified: panel.update("watermarkOpacity", value) }
                Label { text: qsTranslate("Luma", "Size, % of long edge") }
                SpinBox { from: 1; to: 100; value: panel.form.watermarkSize; editable: true; onValueModified: panel.update("watermarkSize", value) }
                Label { text: qsTranslate("Luma", "Margin, % of short edge") }
                SpinBox { from: 0; to: 25; value: panel.form.watermarkMargin; editable: true; onValueModified: panel.update("watermarkMargin", value) }
                Label { text: qsTranslate("Luma", "Position") }
                ComboBox { model: [qsTranslate("Luma", "Top left"), qsTranslate("Luma", "Top center"), qsTranslate("Luma", "Top right"), qsTranslate("Luma", "Left"), qsTranslate("Luma", "Center"), qsTranslate("Luma", "Right"), qsTranslate("Luma", "Bottom left"), qsTranslate("Luma", "Bottom center"), qsTranslate("Luma", "Bottom right")]; currentIndex: panel.form.watermarkAnchor; onActivated: panel.update("watermarkAnchor", currentIndex) }
            }
        }
    }
    footer: ColumnLayout {
        Label { text: qsTranslate("Luma", exporter.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { objectName: "export.close"; text: qsTranslate("Luma", "Close"); onClicked: panel.close() }
            Button { objectName: "export.current"; text: qsTranslate("Luma", "Current photo"); enabled: editor.hasPhoto && !exporter.working; onClicked: { if (exporter.configure(panel.form)) { exporter.start(false); if (exporter.working) panel.close() } } }
            Button { objectName: "export.selection"; text: qsTranslate("Luma", "Selected photos"); enabled: library.selectedCount > 0 && !exporter.working; onClicked: { if (exporter.configure(panel.form)) { exporter.start(true); if (exporter.working) panel.close() } } }
        }
    }
}
