import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: pane
    color: "#111318"
    property var sources: []
    property url destination: ""
    property string currentCollection: ""
    property string editCollectionID: ""
    Connections {
        target: library
        function onReloadStarted(preserveViewport) { grid.captureViewport(preserveViewport) }
        function onViewportRestored(row) { grid.restoreViewport(row) }
    }
    function applyFilter() { library.filter(search.text, ratingFilter.currentIndex, flagFilter.currentIndex - 2, ["any","","red","yellow","green","blue","purple"][labelFilter.currentIndex], currentCollection) }
    function allowShortcut() { return library.selectedCount > 0 && !library.working && !(Window.window.activeFocusItem && typeof Window.window.activeFocusItem.cursorPosition === "number") }
    Shortcut { sequence: "P"; enabled: pane.visible && pane.allowShortcut(); onActivated: library.flag(1) }
    Shortcut { sequence: "X"; enabled: pane.visible && pane.allowShortcut(); onActivated: library.flag(-1) }
    Shortcut { sequence: "U"; enabled: pane.visible && pane.allowShortcut(); onActivated: library.flag(0) }
    Repeater {
        model: 6
        Item { required property int index; Shortcut { sequence: index.toString(); enabled: pane.visible && pane.allowShortcut(); onActivated: library.rate(index) } }
    }
    Shortcut { sequences: [StandardKey.SelectAll]; enabled: pane.visible && !(search.activeFocus); onActivated: library.selectAll() }
    FileDialog { id: files; title: qsTranslate("Luma", "Import photos"); fileMode: FileDialog.OpenFiles; nameFilters: [qsTranslate("Luma", "Photos (*.nef *.NEF *.dng *.DNG *.cr2 *.CR2 *.cr3 *.CR3 *.arw *.ARW *.rw2 *.RW2 *.orf *.ORF *.raf *.RAF *.jpg *.JPG *.jpeg *.JPEG *.png *.PNG *.tif *.TIF *.tiff *.TIFF *.heic *.HEIC *.heif *.HEIF *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.avif *.AVIF)"), qsTranslate("Luma", "All files (*)")]; onAccepted: pane.sources = selectedFiles }
    FolderDialog { id: folder; title: qsTranslate("Luma", "Photo folder"); onAccepted: pane.sources = [selectedFolder] }
    FolderDialog { id: copyFolder; title: qsTranslate("Luma", "Where to copy originals"); onAccepted: pane.destination = selectedFolder }
    Dialog {
        id: importDialog; objectName: "library.importDialog"; title: qsTranslate("Luma", "Import photos"); modal: true; anchors.centerIn: parent; width: 550
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width; spacing: 12
            RowLayout { Button { text: qsTranslate("Luma", "Choose files"); onClicked: files.open() } Button { text: qsTranslate("Luma", "Choose folder"); onClicked: folder.open() } }
            Label { objectName: "library.importSources"; text: pane.sources.length ? qsTranslate("Luma", "Sources: ") + pane.sources.length + " · " + pane.sources[0] : qsTranslate("Luma", "No files or folder selected"); wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 11 }
            ComboBox { id: importMode; objectName: "library.importMode"; model: [qsTranslate("Luma", "Add · keep originals in place"), qsTranslate("Luma", "Copy · copy into the selected folder")]; Layout.fillWidth: true }
            Button { visible: importMode.currentIndex === 1; text: qsTranslate("Luma", "Choose destination folder"); onClicked: copyFolder.open() }
            Label { visible: importMode.currentIndex === 1; text: pane.destination.toString() || qsTranslate("Luma", "No folder selected"); wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 11 }
            CheckBox { id: recursive; objectName: "library.importRecursive"; text: qsTranslate("Luma", "Include subfolders"); checked: true }
            CheckBox { id: duplicates; objectName: "library.importDuplicates"; text: qsTranslate("Luma", "Skip duplicates by SHA-256 and size"); checked: true }
            Label { text: qsTranslate("Luma", "Copy creates a Luma folder with a separate directory per file. Cancelling preserves imported photos."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11; color: "#8893a3" }
            Button { objectName: "library.importStart"; text: qsTranslate("Luma", "Import photos"); enabled: pane.sources.length > 0 && (importMode.currentIndex === 0 || pane.destination.toString().length > 0) && !library.working; onClicked: { library.importFiles(pane.sources, importMode.currentIndex === 1, pane.destination, recursive.checked, duplicates.checked); importDialog.close() } }
        }
    }
    Dialog {
        id: collectionDialog; objectName: "library.collectionDialog"; width: 360; implicitWidth: 360; title: pane.editCollectionID ? qsTranslate("Luma", "Rename collection") : qsTranslate("Luma", "New collection"); modal: true; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { standardButton(Dialog.Ok).objectName = "library.collectionConfirm"; standardButton(Dialog.Cancel).objectName = "library.collectionCancel" }
        TextField { id: collectionName; objectName: "library.collectionName"; width: 300; placeholderText: qsTranslate("Luma", "Collection name"); maximumLength: 128; selectByMouse: true }
        onAccepted: { if (pane.editCollectionID) library.renameCollection(pane.editCollectionID, collectionName.text); else library.createCollection(collectionName.text) }
    }
    Dialog {
        id: deleteDialog; objectName: "library.deleteCollectionDialog"; width: 420; implicitWidth: 420; title: qsTranslate("Luma", "Delete collection?"); modal: true; anchors.centerIn: parent; standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { standardButton(Dialog.Ok).objectName = "library.deleteConfirm"; standardButton(Dialog.Cancel).objectName = "library.deleteCancel" }
        Label { text: qsTranslate("Luma", "Photos and edits will remain in the catalog.") }
        onAccepted: { library.deleteCollection(pane.currentCollection); pane.currentCollection = ""; pane.applyFilter() }
    }
    Dialog {
        id: reportDialog; objectName: "library.importReportDialog"; title: qsTranslate("Luma", "Last import errors"); modal: true; anchors.centerIn: parent
        width: Math.min(pane.width - 40, 700); height: Math.min(pane.height - 40, 450)
        standardButtons: Dialog.Close
        ScrollView { objectName: "library.importReportScroll"; anchors.fill: parent; clip: true; TextArea { objectName: "library.importReportText"; text: library.importReport; readOnly: true; wrapMode: Text.WrapAnywhere; textFormat: TextEdit.PlainText; selectByMouse: true } }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            color: "#191c22"; Layout.preferredWidth: 205; Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                Label { text: qsTranslate("Luma", "LIBRARY"); font.pixelSize: 10; color: "#8290a1"; font.letterSpacing: 1.5 }
                Button { objectName: "library.importButton"; text: qsTranslate("Luma", "+  Import"); Layout.fillWidth: true; enabled: !library.working; onClicked: importDialog.open() }
                Button { text: qsTranslate("Luma", "All photos"); Layout.fillWidth: true; highlighted: !pane.currentCollection; onClicked: { pane.currentCollection = ""; pane.applyFilter() } }
                Label { text: qsTranslate("Luma", "COLLECTIONS"); font.pixelSize: 10; color: "#8290a1"; Layout.topMargin: 16 }
                ScrollView {
                    objectName: "library.collectionsScroll"
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: availableWidth
                    Column {
                        width: parent.width; spacing: 4
                        Repeater {
                            model: library.collections
                            Button { required property var modelData; objectName: "library.collection." + modelData.id; width: parent.width; text: modelData.name + " · " + modelData.count; highlighted: pane.currentCollection === modelData.id; onClicked: { pane.currentCollection = modelData.id; pane.applyFilter() } }
                        }
                    }
                }
                Button { objectName: "library.newCollection"; text: qsTranslate("Luma", "+ Collection"); Layout.fillWidth: true; enabled: !library.working; onClicked: { pane.editCollectionID = ""; collectionName.text = ""; collectionDialog.open() } }
                Button { objectName: "library.renameCollection"; text: qsTranslate("Luma", "Rename"); visible: !!pane.currentCollection; enabled: !library.working; Layout.fillWidth: true; onClicked: { pane.editCollectionID = pane.currentCollection; const item = library.collections.find(c => c.id === pane.currentCollection); collectionName.text = item ? item.name : ""; collectionDialog.open() } }
                Button { objectName: "library.deleteCollection"; text: qsTranslate("Luma", "Delete collection"); visible: !!pane.currentCollection; enabled: !library.working; Layout.fillWidth: true; onClicked: deleteDialog.open() }
                Label { text: qsTranslate("Luma", "Double-click → Develop\n⌘ / Shift → select\n0–5 · P / X / U"); color: "#8290a1"; font.pixelSize: 10; Layout.topMargin: 8 }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.margins: 18; spacing: 12
            RowLayout {
                Layout.minimumWidth: 0; Layout.fillWidth: true
                TextField { id: search; objectName: "library.search"; Layout.minimumWidth: 0; placeholderTextColor: "#858c97"; placeholderText: qsTranslate("Luma", "Search: name, path, camera, lens"); selectByMouse: true; maximumLength: 1024; Layout.fillWidth: true; onTextChanged: pane.applyFilter() }
                ComboBox { id: ratingFilter; objectName: "library.ratingFilter"; Layout.minimumWidth: 0; Layout.maximumWidth: 110; model: [qsTranslate("Luma", "All ★"), "≥ 1 ★", "≥ 2 ★", "≥ 3 ★", "≥ 4 ★", "5 ★"]; onActivated: pane.applyFilter() }
                ComboBox { id: flagFilter; objectName: "libraryFlagFilter"; Layout.minimumWidth: 0; Layout.maximumWidth: 140; ToolTip.visible: hovered; ToolTip.text: currentText; model: [qsTranslate("Luma", "All flags"), qsTranslate("Luma", "Reject"), qsTranslate("Luma", "Unflagged"), qsTranslate("Luma", "Pick")]; onActivated: pane.applyFilter() }
                ComboBox { id: labelFilter; objectName: "libraryLabelFilter"; Layout.minimumWidth: 0; Layout.maximumWidth: 140; ToolTip.visible: hovered; ToolTip.text: currentText; model: [qsTranslate("Luma", "All labels"), qsTranslate("Luma", "No label"), qsTranslate("Luma", "Red"), qsTranslate("Luma", "Yellow"), qsTranslate("Luma", "Green"), qsTranslate("Luma", "Blue"), qsTranslate("Luma", "Purple")]; onActivated: pane.applyFilter() }
            }
            RowLayout {
                visible: library.importing; Layout.fillWidth: true
                ProgressBar { value: library.progress; indeterminate: library.progress === 0; Layout.fillWidth: true }
                Button { objectName: "library.cancelImport"; text: qsTranslate("Luma", "Cancel import"); onClicked: library.cancelImport() }
            }
            Label { objectName: "library.error"; visible: library.error.length > 0; text: qsTranslate("Luma", library.error); color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Layout.minimumWidth: 0; Layout.fillWidth: true
                Button { objectName: "library.importReportButton"; visible: library.importReport.length > 0; text: qsTranslate("Luma", "Import errors"); onClicked: reportDialog.open() }
                Label { text: library.loading ? qsTranslate("Luma", "Loading…") : library.count + qsTranslate("Luma", " photos · selected ") + library.selectedCount; color: "#98a3b3"; Layout.fillWidth: true }
                Button { objectName: "library.selectAll"; text: qsTranslate("Luma", "Select all"); enabled: library.count > 0; onClicked: library.selectAll() }
                Button { objectName: "library.clearSelection"; text: qsTranslate("Luma", "Clear selection"); enabled: library.selectedCount > 0; onClicked: library.clearSelection() }
            }
            BatchSettings {}
            Label { text: qsTranslate("Luma", batch.status); textFormat: Text.PlainText; visible: text.length > 0; color: "#98a3b3"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: qsTranslate("Luma", batch.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                GridView {
                    id: grid; objectName: "library.grid"; anchors.fill: parent; clip: true; model: library
                    property string viewportAsset: ""
                    property real viewportOffset: 0
                    property bool restorePending: false
                    property int restoreRevision: 0
                    function cancelViewportRestore() {
                        ++restoreRevision
                        restorePending = false
                    }
                    function captureViewport(preserve) {
                        ++restoreRevision
                        if (!preserve || dragging || libraryBar.pressed) {
                            restorePending = false
                            viewportAsset = ""
                        } else {
                            let card = null
                            for (const candidate of contentItem.children) {
                                if (candidate instanceof PhotoCard && candidate.assetID.length &&
                                    candidate.y + candidate.height > contentY && candidate.y < contentY + height &&
                                    (!card || candidate.index < card.index))
                                    card = candidate
                            }
                            if (card && card.assetID.length) {
                                viewportAsset = card.assetID
                                viewportOffset = card.y - contentY
                                restorePending = true
                            } else if (!restorePending) {
                                // Keep a pending identity across another reload before
                                // delegates for the preceding result have been laid out.
                                viewportAsset = ""
                            }
                        }
                        library.setViewportAnchor(restorePending ? viewportAsset : "")
                    }
                    function restoreViewport(row) {
                        const revision = restoreRevision
                        Qt.callLater(function() {
                            if (revision !== restoreRevision || !restorePending)
                                return
                            restorePending = false
                            if (row < 0 || row >= count)
                                return
                            cancelFlick()
                            forceLayout()
                            positionViewAtIndex(row, GridView.Beginning)
                            contentY = Math.max(originY, Math.min(originY + Math.max(0, contentHeight - height), contentY - viewportOffset))
                        })
                    }
                    onMovementStarted: cancelViewportRestore()
                    cellWidth: width / Math.max(1,Math.floor(width / 220)); cellHeight: 210
                    cacheBuffer: 420
                    delegate: PhotoCard { width: grid.cellWidth - 12; height: grid.cellHeight - 12 }
                    ScrollBar.vertical: ScrollBar {
                        id: libraryBar
                        objectName: "library.scrollBar"
                        onPressedChanged: { if (pressed) grid.cancelViewportRestore() }
                    }
                }
                Label { anchors.centerIn: parent; visible: !library.loading && !library.count; text: qsTranslate("Luma", "Import photos\nor change the filters"); horizontalAlignment: Text.AlignHCenter; color: "#8290a1"; font.pixelSize: 18 }
            }
            GridLayout {
                objectName: "library.metadataActions"
                Layout.minimumWidth: 0; Layout.fillWidth: true
                columns: width < 900 ? 1 : 2; columnSpacing: 12; rowSpacing: 6
                enabled: library.selectedCount > 0 && !library.working
                RowLayout {
                    Layout.minimumWidth: 0; Layout.fillWidth: true; spacing: 6
                    Label { Layout.maximumWidth: 80; elide: Text.ElideRight; text: qsTranslate("Luma", "Rating"); font.pixelSize: 11 }
                    Repeater { model: 6; Button { required property int index; Accessible.name: qsTranslate("Luma", "Rating") + " " + index; text: index ? index + "★" : "0"; implicitWidth: 38; onClicked: library.rate(index) } }
                    ComboBox { Layout.minimumWidth: 0; Layout.maximumWidth: 120; model: [qsTranslate("Luma", "Flag…"), qsTranslate("Luma", "Pick"), qsTranslate("Luma", "Reject"), qsTranslate("Luma", "Unflagged")]; onActivated: { if (currentIndex > 0) library.flag([0,1,-1,0][currentIndex]); currentIndex = 0 } }
                    ComboBox { objectName: "library.labelChoice"; Layout.minimumWidth: 0; Layout.maximumWidth: 120; model: [qsTranslate("Luma", "Label…"), qsTranslate("Luma", "No label"), qsTranslate("Luma", "Red"), qsTranslate("Luma", "Yellow"), qsTranslate("Luma", "Green"), qsTranslate("Luma", "Blue"), qsTranslate("Luma", "Purple")]; onActivated: { if (currentIndex > 0) library.label(["","","red","yellow","green","blue","purple"][currentIndex]); currentIndex = 0 } }
                }
                RowLayout {
                    Layout.minimumWidth: 0; Layout.fillWidth: true; spacing: 6
                    ComboBox { id: targetCollection; Layout.minimumWidth: 0; model: library.collections; textRole: "name"; Layout.maximumWidth: 145 }
                    Button { objectName: "libraryAddToCollection"; implicitWidth: 36; text: "+"; enabled: library.collections.length > 0; onClicked: library.collectionMembership(library.collections[targetCollection.currentIndex].id,true); Accessible.name: qsTranslate("Luma", "Add to collection") }
                    Button { objectName: "libraryRemoveFromCollection"; implicitWidth: 36; text: "−"; enabled: !!pane.currentCollection; onClicked: library.collectionMembership(pane.currentCollection,false); Accessible.name: qsTranslate("Luma", "Remove from collection") }
                }
            }
            Label { text: qsTranslate("Luma", library.status); font.pixelSize: 10; color: "#8290a1"; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }
}
