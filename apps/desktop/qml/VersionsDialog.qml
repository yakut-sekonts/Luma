import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "versions.dialog"
    title: qsTranslate("Luma", "Processing versions")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 640)
    height: Math.min(parent.height - 40, 660)
    standardButtons: Dialog.Close
    property string selectedID: ""
    property bool wasBusy: false
    function selected() {
        for (let i = 0; i < editor.processingVersions.length; ++i)
            if (editor.processingVersions[i].id === selectedID) return editor.processingVersions[i]
        return null
    }
    onOpened: {
        selectedID = ""
        nameField.text = ""
        editor.refreshVersions()
    }
    Connections {
        target: editor
        function onPhotoLoaded(id) { deletion.close(); dialog.close() }
        function onStateChanged() {
            if (dialog.wasBusy && !editor.versionsBusy) dialog.selectedID = editor.selectedVersionID
            dialog.wasBusy = editor.versionsBusy
        }
    }
    Dialog {
        id: deletion
        objectName: "versions.deleteConfirmation"
        title: qsTranslate("Luma", "Delete version?")
        modal: true
        anchors.centerIn: parent
        width: Math.min(dialog.width - 30, 480)
        standardButtons: Dialog.Yes | Dialog.Cancel
        property string versionID: ""
        property string revision: ""
        property string versionName: ""
        contentItem: Label {
            text: qsTranslate("Luma", "Delete “%1”? The current edit and original photo will be kept.").arg(deletion.versionName)
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }
        onAccepted: editor.deleteVersion(versionID, revision)
    }
    contentItem: ScrollView {
        id: body
        clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: body.availableWidth
            spacing: 10
            Label { text: editor.filename; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label {
                text: qsTranslate("Luma", "Versions keep all processing settings, masks and retouch without copying the original photo.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: nameField
                    objectName: "versions.name"
                    placeholderText: qsTranslate("Luma", "Version name")
                    Accessible.name: qsTranslate("Luma", "Version name")
                    maximumLength: 512
                    enabled: !editor.busy
                    Layout.fillWidth: true
                }
                Button {
                    objectName: "versions.save"
                    text: qsTranslate("Luma", "Save current edit")
                    enabled: editor.hasPhoto && !editor.busy && nameField.text.trim().length > 0
                    onClicked: editor.saveVersion(nameField.text)
                }
            }
            ListView {
                id: versionsList
                objectName: "versions.list"
                model: editor.processingVersions
                clip: true
                Layout.fillWidth: true; Layout.preferredHeight: 200
                spacing: 4
                ScrollBar.vertical: ScrollBar { }
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    highlighted: dialog.selectedID === modelData.id
                    enabled: !editor.busy
                    Accessible.name: modelData.name
                    contentItem: ColumnLayout {
                        Label { text: modelData.name; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                        Label { text: Qt.formatDateTime(new Date(modelData.createdAt), Qt.DefaultLocaleShortDate); font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    onClicked: { dialog.selectedID = modelData.id; nameField.text = modelData.name }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width
                    visible: versionsList.count === 0 && !editor.versionsBusy
                    text: qsTranslate("Luma", "No saved versions for this photo.")
                    wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "versions.restore"
                    text: qsTranslate("Luma", "Restore version")
                    enabled: !editor.busy && dialog.selected() !== null
                    Layout.fillWidth: true
                    onClicked: { const item = dialog.selected(); if (item) editor.restoreVersion(item.id, item.revision) }
                }
                Button {
                    objectName: "versions.rename"
                    text: qsTranslate("Luma", "Rename")
                    enabled: !editor.busy && dialog.selected() !== null && nameField.text.trim().length > 0
                    Layout.fillWidth: true
                    onClicked: { const item = dialog.selected(); if (item) editor.renameVersion(item.id, item.revision, nameField.text) }
                }
                Button {
                    objectName: "versions.delete"
                    text: qsTranslate("Luma", "Delete")
                    enabled: !editor.busy && dialog.selected() !== null
                    Layout.fillWidth: true
                    onClicked: {
                        const item = dialog.selected()
                        if (!item) return
                        deletion.versionID = item.id; deletion.revision = item.revision; deletion.versionName = item.name
                        deletion.open()
                    }
                }
            }
            Label {
                text: qsTranslate("Luma", "Up to 100 versions and 256 MiB per photo. Versions exclude ratings, keywords and other photo metadata.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            ProgressBar { indeterminate: true; visible: editor.versionsBusy; Layout.fillWidth: true }
            Label { text: editor.versionsStatus; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", editor.versionsError); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Refresh"); enabled: !editor.busy; onClicked: editor.refreshVersions(); Layout.fillWidth: true }
        }
    }
}
