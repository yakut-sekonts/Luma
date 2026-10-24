import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "history.dialog"
    title: qsTranslate("Luma", "History")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 40, 660)
    height: Math.min(parent.height - 40, 720)
    standardButtons: Dialog.Close
    readonly property var editorScrollView: body
    property var rows: []
    property string selectedID: ""
    property string shownRevision: ""
    property bool updatingRows: false
    function selected() {
        for (let i = 0; i < rows.length; ++i)
            if (rows[i].id === selectedID) return rows[i]
        return null
    }
    function refreshRows(force) {
        if (!visible || (!force && shownRevision === editor.historyRevision)) return
        const position = historyList.contentY
        const selection = selectedID
        updatingRows = true
        rows = editor.processingHistory
        shownRevision = editor.historyRevision
        let selectedIndex = -1
        let currentIndex = -1
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === selection) selectedIndex = i
            if (rows[i].current) currentIndex = i
        }
        if (selectedIndex < 0) selectedIndex = currentIndex
        selectedID = selectedIndex >= 0 ? rows[selectedIndex].id : ""
        historyList.currentIndex = selectedIndex
        updatingRows = false
        // Render/autosave notifications must not reset a scrolled history list.
        Qt.callLater(function() {
            if (!dialog.visible) return
            historyList.contentY = Math.max(historyList.originY,
                Math.min(position, historyList.originY + Math.max(0, historyList.contentHeight - historyList.height)))
        })
    }
    onOpened: {
        selectedID = ""
        nameField.text = ""
        editor.prepareHistory()
        refreshRows(true)
        Qt.callLater(function() { historyList.positionViewAtIndex(historyList.currentIndex, ListView.Contain) })
    }
    onClosed: clearConfirmation.close()
    Connections {
        target: editor
        function onPhotoLoaded(id) { clearConfirmation.close(); dialog.close() }
        function onStateChanged() { dialog.refreshRows(false) }
    }
    Connections {
        target: localization
        function onChanged() { dialog.refreshRows(true) }
    }
    Dialog {
        id: clearConfirmation
        objectName: "history.clearConfirmation"
        title: qsTranslate("Luma", "Clear history?")
        modal: true
        anchors.centerIn: parent
        width: Math.min(dialog.width - 30, 500)
        implicitHeight: Math.min(dialog.height - 40, 260)
        standardButtons: Dialog.Yes | Dialog.Cancel
        property string revision: ""
        contentItem: ScrollView {
            id: clearWarning
            clip: true
            contentWidth: availableWidth
            Label {
                width: clearWarning.availableWidth
                text: qsTranslate("Luma", "Remove this photo's session history? Current edits, saved versions and the original are kept. This cannot be undone. Batch history is separate.")
                wrapMode: Text.Wrap
            }
        }
        onAccepted: editor.clearHistory(revision)
    }
    contentItem: ScrollView {
        id: body
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: body.availableWidth
            spacing: 10
            Label { text: editor.filename; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label {
                text: qsTranslate("Luma", "History keeps up to 200 steps for the open photo. Opening another photo, restarting or applying a batch change starts a new history. Save a version to keep a state.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    objectName: "history.undo"
                    text: qsTranslate("Luma", "Undo")
                    enabled: !editor.busy && !editor.editingLocked && editor.canUndo
                    Layout.fillWidth: true
                    onClicked: editor.undo()
                }
                Button {
                    objectName: "history.redo"
                    text: qsTranslate("Luma", "Redo")
                    enabled: !editor.busy && !editor.editingLocked && editor.canRedo
                    Layout.fillWidth: true
                    onClicked: editor.redo()
                }
            }
            ListView {
                id: historyList
                objectName: "history.list"
                model: dialog.rows
                clip: true
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                spacing: 4
                keyNavigationEnabled: true
                activeFocusOnTab: true
                ScrollBar.vertical: ScrollBar { }
                onCurrentIndexChanged: {
                    if (!dialog.updatingRows && currentIndex >= 0 && currentIndex < dialog.rows.length)
                        dialog.selectedID = dialog.rows[currentIndex].id
                }
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: dialog.selectedID === modelData.id
                    enabled: !editor.busy && !editor.editingLocked
                    Accessible.name: modelData.title + (modelData.current ? " · " + qsTranslate("Luma", "Current state") : "")
                    contentItem: ColumnLayout {
                        Label { text: modelData.title; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            text: modelData.current ? qsTranslate("Luma", "Current state") : (modelData.redo ? qsTranslate("Luma", "Available through Redo") : "")
                            visible: text.length > 0
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                    onClicked: { historyList.currentIndex = index; dialog.selectedID = modelData.id }
                }
            }
            Label {
                readonly property var entry: dialog.selected()
                text: entry ? entry.title + (entry.details.length > 0 ? "\n" + entry.details : "") : ""
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Button {
                objectName: "history.jump"
                text: qsTranslate("Luma", "Go to selected state")
                enabled: !editor.busy && !editor.editingLocked && dialog.selected() !== null
                Layout.fillWidth: true
                onClicked: editor.jumpToHistory(dialog.selectedID)
            }
            Label {
                text: qsTranslate("Luma", "Moving through history keeps later steps available through Redo. A new edit replaces those later steps.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            TextField {
                id: nameField
                objectName: "history.versionName"
                placeholderText: qsTranslate("Luma", "Version name")
                Accessible.name: qsTranslate("Luma", "Version name")
                maximumLength: 512
                enabled: !editor.busy
                Layout.fillWidth: true
            }
            Button {
                objectName: "history.saveVersion"
                text: qsTranslate("Luma", "Save selected state as version")
                enabled: !editor.busy && !editor.editingLocked && dialog.selected() !== null && nameField.text.trim().length > 0
                Layout.fillWidth: true
                onClicked: editor.saveHistoryVersion(dialog.selectedID, nameField.text)
            }
            ProgressBar { indeterminate: true; visible: editor.versionsBusy; Layout.fillWidth: true }
            Label { text: editor.versionsStatus; textFormat: Text.PlainText; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", editor.versionsError); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", editor.historyError); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Button {
                objectName: "history.clear"
                text: qsTranslate("Luma", "Clear history")
                enabled: !editor.busy && !editor.editingLocked && (editor.canUndo || editor.canRedo)
                Layout.fillWidth: true
                onClicked: { clearConfirmation.revision = editor.historyRevision; clearConfirmation.open() }
            }
        }
    }
}
