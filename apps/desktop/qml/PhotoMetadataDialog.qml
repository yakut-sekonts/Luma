import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "photoMetadata"
    title: qsTranslate("Luma", "Photo metadata")
    modal: true; anchors.centerIn: parent
    width: Math.min(parent.width - 40, 610)
    height: Math.min(parent.height - 40, 720)
    standardButtons: Dialog.Close
    readonly property bool ready: !!library.descriptiveMetadata.id && !library.working
    function showPhoto(id) { library.loadMetadata(id); open() }
    Connections {
        target: library
        function onMetadataChanged() {
            const values = library.descriptiveMetadata
            titleField.text = values.title || ""; captionField.text = values.caption || ""
            copyrightField.text = values.copyright || ""; creatorsField.text = (values.creators || []).join("\n")
            locationField.text = values.location || ""; keywordsField.text = (values.keywords || []).join("\n")
            ratingField.currentIndex = values.rating || 0
            flagField.currentIndex = (values.flag || 0) + 1
            labelField.text = values.label || ""
        }
    }
    FileDialog { id: locateFile; objectName: "metadata.locateFileDialog"; title: qsTranslate("Luma", "Locate original"); onAccepted: library.locateOriginal(selectedFile) }
    FolderDialog { id: locateFolder; objectName: "metadata.locateFolderDialog"; title: qsTranslate("Luma", "Locate folder"); onAccepted: library.locateFolder(selectedFolder, nearby.checked) }
    Dialog {
        id: confirmImport
        objectName: "metadata.confirmRead"
        title: qsTranslate("Luma", "Read metadata from original and XMP?")
        modal: true; anchors.centerIn: parent; width: Math.min(dialog.width - 40, 480)
        implicitHeight: Math.min(dialog.height - 40, 260)
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: ScrollView {
            id: importWarning
            clip: true
            contentWidth: availableWidth
            Label {
                width: importWarning.availableWidth; wrapMode: Text.Wrap
                text: qsTranslate("Luma", "Replaces the catalog metadata and this draft with the values from the original and sidecar. Develop edits remain unchanged.")
            }
        }
        onAccepted: library.importDescriptiveMetadata()
    }
    contentItem: ScrollView {
        objectName: "metadata.scroll"
        clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: parent.width; spacing: 8
            Label { objectName: "metadata.path"; text: library.descriptiveMetadata.path || ""; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", library.descriptiveMetadata.readError || ""); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { objectName: "metadata.error"; text: qsTranslate("Luma", library.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "Title") }
            TextField { id: titleField; objectName: "metadata.title"; enabled: dialog.ready; Layout.fillWidth: true; maximumLength: 1024; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Title") }
            Label { text: qsTranslate("Luma", "Caption") }
            TextArea { id: captionField; objectName: "metadata.caption"; enabled: dialog.ready; Layout.fillWidth: true; Layout.minimumHeight: 75; wrapMode: Text.Wrap; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Caption") }
            Label { text: qsTranslate("Luma", "Creators · one per line") }
            TextArea { id: creatorsField; objectName: "metadata.creators"; enabled: dialog.ready; Layout.fillWidth: true; Layout.minimumHeight: 65; textFormat: TextEdit.PlainText; wrapMode: Text.Wrap; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Creators · one per line") }
            Label { text: qsTranslate("Luma", "Copyright") }
            TextField { id: copyrightField; objectName: "metadata.copyright"; enabled: dialog.ready; Layout.fillWidth: true; maximumLength: 2048; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Copyright") }
            Label { text: qsTranslate("Luma", "Location") }
            TextField { id: locationField; objectName: "metadata.location"; enabled: dialog.ready; Layout.fillWidth: true; maximumLength: 2048; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Location") }
            Label { text: qsTranslate("Luma", "Keywords · one per line") }
            TextArea { id: keywordsField; objectName: "metadata.keywords"; enabled: dialog.ready; Layout.fillWidth: true; Layout.minimumHeight: 75; wrapMode: Text.Wrap; selectByMouse: true; Accessible.name: qsTranslate("Luma", "Keywords · one per line") }
            RowLayout {
                Layout.fillWidth: true
                ComboBox { id: ratingField; objectName: "metadata.rating"; enabled: dialog.ready; model: ["0 ★", "1 ★", "2 ★", "3 ★", "4 ★", "5 ★"]; Accessible.name: qsTranslate("Luma", "Rating"); Layout.fillWidth: true }
                ComboBox { id: flagField; objectName: "metadata.flag"; enabled: dialog.ready; model: [qsTranslate("Luma", "Reject"), qsTranslate("Luma", "Unflagged"), qsTranslate("Luma", "Pick")]; Accessible.name: qsTranslate("Luma", "Flag"); Layout.fillWidth: true }
            }
            Label { text: qsTranslate("Luma", "XMP stores Reject as -1 instead of a star rating. Pick stays in the catalog."); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTranslate("Luma", "XMP label") }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: labelField; objectName: "metadata.label"; enabled: dialog.ready; Layout.fillWidth: true; maximumLength: 1024; selectByMouse: true; Accessible.name: qsTranslate("Luma", "XMP label") }
                ComboBox {
                    enabled: dialog.ready; Layout.preferredWidth: 160
                    model: [qsTranslate("Luma", "No label"), qsTranslate("Luma", "Red"), qsTranslate("Luma", "Yellow"), qsTranslate("Luma", "Green"), qsTranslate("Luma", "Blue"), qsTranslate("Luma", "Purple")]
                    currentIndex: ["", "red", "yellow", "green", "blue", "purple"].indexOf(labelField.text.toLowerCase())
                    onActivated: labelField.text = ["", "Red", "Yellow", "Green", "Blue", "Purple"][currentIndex]
                    Accessible.name: qsTranslate("Luma", "Color label")
                }
            }
            Label { text: qsTranslate("Luma", "Custom labels are preserved. Standard color names also color the photo card."); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Button {
                objectName: "metadata.save"
                text: qsTranslate("Luma", "Save metadata to catalog"); enabled: dialog.ready; Layout.fillWidth: true
                onClicked: library.saveDescriptiveMetadata({title: titleField.text, caption: captionField.text, creators: creatorsField.text.split("\n"),
                    copyright: copyrightField.text, location: locationField.text, keywords: keywordsField.text.split("\n"),
                    rating: ratingField.currentIndex, flag: flagField.currentIndex - 1, label: labelField.text})
            }
            RowLayout {
                Layout.fillWidth: true
                Button { objectName: "metadata.read"; text: qsTranslate("Luma", "Read original / XMP"); enabled: dialog.ready; onClicked: confirmImport.open(); Layout.fillWidth: true }
                Button { objectName: "metadata.write"; text: qsTranslate("Luma", "Write saved metadata to XMP"); enabled: dialog.ready; onClicked: library.writeMetadataSidecar(); Layout.fillWidth: true }
            }
            Button { text: qsTranslate("Luma", "Reload catalog values"); enabled: dialog.ready; onClicked: library.loadMetadata(library.descriptiveMetadata.id); Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "Missing original"); font.bold: true; Layout.topMargin: 10 }
            RowLayout {
                Layout.fillWidth: true
                Button { objectName: "metadata.locateOriginal"; text: qsTranslate("Luma", "Locate original"); enabled: dialog.ready; onClicked: locateFile.open(); Layout.fillWidth: true }
                Button { objectName: "metadata.locateFolder"; text: qsTranslate("Luma", "Locate folder"); enabled: dialog.ready; onClicked: locateFolder.open(); Layout.fillWidth: true }
            }
            CheckBox { id: nearby; objectName: "metadata.locateNearby"; text: qsTranslate("Luma", "Also find nearby missing files by name and checksum"); checked: true; Layout.fillWidth: true }
        }
    }
}
