import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Rectangle {
    id: pane
    color: "#111318"
    property string currentCollection: ""
    property string editCollectionID: ""
    CompareDialog { id:compareDialog }
    SmartCollectionDialog { id: smartCollection }
    SemanticSearchDialog { id: semanticSearch }
    PhotoMetadataDialog { id: photoMetadata }
    Dialog {
        id: panoramaDialog; objectName: "merge.panorama"
        title: qsTranslate("Luma", "Panorama merge")
        modal: true; anchors.centerIn: parent
        width: Math.min(pane.width - 40, 460)
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTranslate("Luma", "Merge 2–24 overlapping originals in capture order. Creates an auto-cropped Float DNG without current Develop edits.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            ComboBox {
                id: panoramaProjection; objectName: "merge.projection"
                model: [qsTranslate("Luma", "Perspective"), qsTranslate("Luma", "Cylindrical"), qsTranslate("Luma", "Spherical")]
                currentIndex: 1; Layout.fillWidth: true
                Accessible.name: qsTranslate("Luma", "Projection")
            }
            Label { text: qsTranslate("Luma", "Frame field of view") + " · " + Math.round(panoramaFOV.value) + "°" }
            Slider {
                id: panoramaFOV; from: 10; to: 140; value: 60; stepSize: 1
                enabled: panoramaProjection.currentIndex !== 0
                Layout.fillWidth: true; Accessible.name: qsTranslate("Luma", "Frame field of view")
            }
            CheckBox { id: panoramaWarp; text: qsTranslate("Luma", "Boundary warp · stretch edges to reduce cropping"); Layout.fillWidth: true }
            Button {
                objectName: "merge.panorama.create"
                text: qsTranslate("Luma", "Create panorama")
                enabled: library.selectedCount >= 2 && library.selectedCount <= 24 && !library.working && !editor.aiBusy && !editor.busy
                Layout.fillWidth: true
                onClicked: { editor.mergePanoramaPhotos(library.selectedAssetIDs(), panoramaProjection.currentIndex, panoramaFOV.value, panoramaWarp.checked); panoramaDialog.close() }
            }
        }
    }
    Dialog {
        id: hdrDialog; objectName: "merge.hdr"
        title: qsTranslate("Luma", "HDR merge")
        modal: true; anchors.centerIn: parent
        width: Math.min(pane.width - 40, 440)
        standardButtons: Dialog.Cancel
        ColumnLayout {
            width: parent.width
            Label {
                text: qsTranslate("Luma", "Merge 2–9 bracketed originals into a new editable Float DNG. Current Develop edits are not applied.")
                wrapMode: Text.Wrap; Layout.fillWidth: true
            }
            Label { text: qsTranslate("Luma", "Deghosting") + " · " + Math.round(hdrDeghost.value) + "%" }
            CheckBox {
                id: hdrAlignment; objectName: "merge.alignment"
                text: qsTranslate("Luma", "Align frames"); checked: true
                Layout.fillWidth: true
            }
            Slider { id: hdrDeghost; objectName: "merge.deghost"; from: 0; to: 100; value: 50; Layout.fillWidth: true; Accessible.name: qsTranslate("Luma", "Deghosting") }
            Button {
                objectName: "merge.hdr.create"
                text: qsTranslate("Luma", "Create HDR")
                enabled: library.selectedCount >= 2 && library.selectedCount <= 9 && !library.working && !editor.aiBusy && !editor.busy
                Layout.fillWidth: true
                onClicked: { editor.mergeHDRPhotos(library.selectedAssetIDs(), hdrDeghost.value, hdrAlignment.checked); hdrDialog.close() }
            }
        }
    }
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
    ImportDialog { id: importDialog }
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
                Button { objectName: "semantic.open"; text: qsTranslate("Luma", "Search by description"); Layout.fillWidth: true; onClicked: semanticSearch.open() }
                Button {
                    objectName: "library.photoActions"
                    text: qsTranslate("Luma", "Photo actions")
                    Layout.fillWidth: true
                    onClicked: photoActions.open()
                    Menu {
                        id: photoActions
                        MenuItem { objectName: "library.photoMetadata"; text: qsTranslate("Luma", "Photo metadata"); enabled: library.selectedCount === 1 && !library.working; onTriggered: photoMetadata.showPhoto(library.selectedAssetIDs()[0]) }
                        MenuItem { text: qsTranslate("Luma", "Compare Select / Candidate"); enabled: library.selectedCount === 2 && !library.working && !editor.busy; onTriggered: {editor.prepareCompare(library.selectedAssetIDs());compareDialog.open()} }
                        MenuItem { objectName: "library.hdr"; text: qsTranslate("Luma", "HDR merge"); enabled: library.selectedCount >= 2 && library.selectedCount <= 9 && !library.working && !editor.aiBusy && !editor.busy; onTriggered: hdrDialog.open() }
                        MenuItem { objectName: "library.panorama"; text: qsTranslate("Luma", "Panorama merge"); enabled: library.selectedCount >= 2 && library.selectedCount <= 24 && !library.working && !editor.aiBusy && !editor.busy; onTriggered: panoramaDialog.open() }
                    }
                }
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
                Button {text:qsTranslate("Luma","+ Smart Collection");Layout.fillWidth:true;enabled:!library.working;onClicked:smartCollection.create()}
                Button {
                    text:qsTranslate("Luma","Edit rules");Layout.fillWidth:true;enabled:!library.working
                    visible:!!pane.currentCollection && library.collections.some(c=>c.id===pane.currentCollection&&c.smart)
                    onClicked:{const c=library.collections.find(c=>c.id===pane.currentCollection);if(c)library.editSmartCollection(c.id,c.name)}
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
            RowLayout {
                visible: editor.aiBusy && (editor.aiOperation === "HDR merge" || editor.aiOperation === "Panorama merge")
                Layout.fillWidth: true
                Label { objectName: "merge.operation"; text: qsTranslate("Luma", editor.aiOperation) }
                Label {
                    objectName: "merge.phase"
                    text: editor.aiProgress < 0.25 ? qsTranslate("Luma", "Aligning originals…")
                        : editor.aiProgress < 0.85 ? qsTranslate("Luma", "Merging frames…")
                        : qsTranslate("Luma", "Writing Float DNG…")
                }
                ProgressBar { objectName: "merge.progress"; value: editor.aiProgress; Layout.fillWidth: true }
                Button { objectName: "merge.cancel"; text: qsTranslate("Luma", "Cancel"); onClicked: editor.cancelAI() }
            }
            Label { text: qsTranslate("Luma", batch.status); textFormat: Text.PlainText; visible: text.length > 0; color: "#98a3b3"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
            Label { text: qsTranslate("Luma", batch.error); textFormat: Text.PlainText; visible: text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                GridView {
                    id: grid; objectName: "library.grid"; anchors.fill: parent; clip: true; model: library
                    currentIndex: -1
                    property string viewportAsset: ""
                    property real viewportOffset: 0
                    property bool restorePending: false
                    property int restoreRevision: 0
                    property int geometryAnchorRow: -1
                    property real geometryAnchorOffset: 0
                    property bool geometryRestorePending: false
                    property int geometryRevision: 0
                    function firstViewportCard() {
                        let card = null
                        for (const candidate of contentItem.children) {
                            if (candidate instanceof PhotoCard && candidate.assetID.length &&
                                candidate.y + candidate.height > contentY && candidate.y < contentY + height &&
                                (!card || candidate.index < card.index))
                                card = candidate
                        }
                        return card
                    }
                    function captureGeometryAnchor() {
                        if (restorePending || geometryRestorePending || library.loading || moving || libraryBar.pressed)
                            return
                        const card = firstViewportCard()
                        geometryAnchorRow = card ? card.index : -1
                        geometryAnchorOffset = card ? card.y - contentY : 0
                    }
                    function cancelGeometryRestore() {
                        ++geometryRevision
                        geometryRestorePending = false
                    }
                    function restoreGeometryAnchor() {
                        if (geometryAnchorRow < 0 || restorePending || library.loading || moving || libraryBar.pressed)
                            return
                        const revision = ++geometryRevision
                        geometryRestorePending = true
                        Qt.callLater(function() {
                            if (revision !== geometryRevision)
                                return
                            geometryRestorePending = false
                            if (restorePending || library.loading || geometryAnchorRow >= count)
                                return
                            forceLayout()
                            positionViewAtIndex(geometryAnchorRow, GridView.Beginning)
                            contentY = Math.max(originY, Math.min(originY + Math.max(0, contentHeight - height), contentY - geometryAnchorOffset))
                        })
                    }
                    function cancelViewportRestore() {
                        ++restoreRevision
                        restorePending = false
                    }
                    function captureViewport(preserve) {
                        cancelGeometryRestore()
                        geometryAnchorRow = -1
                        ++restoreRevision
                        if (!preserve || dragging || libraryBar.pressed) {
                            restorePending = false
                            viewportAsset = ""
                        } else {
                            const card = firstViewportCard()
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
                            if (row < 0 || row >= count) {
                                captureGeometryAnchor()
                                return
                            }
                            cancelFlick()
                            forceLayout()
                            positionViewAtIndex(row, GridView.Beginning)
                            contentY = Math.max(originY, Math.min(originY + Math.max(0, contentHeight - height), contentY - viewportOffset))
                            // The model has resolved this identity even if its
                            // metadata page has not populated the delegate yet.
                            geometryAnchorRow = row
                            geometryAnchorOffset = viewportOffset
                        })
                    }
                    // Keep the user's row across responsive column-count changes,
                    // including changes while Library is hidden behind Develop.
                    // Reflow must not replace that row with the new row's first cell.
                    onWidthChanged: restoreGeometryAnchor()
                    onHeightChanged: restoreGeometryAnchor()
                    onVisibleChanged: { if (visible) restoreGeometryAnchor() }
                    onMovementStarted: { cancelViewportRestore(); cancelGeometryRestore() }
                    onMovementEnded: captureGeometryAnchor()
                    cellWidth: width / Math.max(1,Math.floor(width / 220)); cellHeight: 210
                    cacheBuffer: 420
                    delegate: PhotoCard { width: grid.cellWidth - 12; height: grid.cellHeight - 12 }
                    ScrollBar.vertical: ScrollBar {
                        id: libraryBar
                        objectName: "library.scrollBar"
                        onPressedChanged: {
                            if (pressed) {
                                grid.cancelViewportRestore()
                                grid.cancelGeometryRestore()
                            } else {
                                grid.captureGeometryAnchor()
                            }
                        }
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
                    Repeater { model: 6; Button { required property int index; objectName: "library.rate." + index; Accessible.name: qsTranslate("Luma", "Rating") + " " + index; text: index ? index + "★" : "0"; implicitWidth: 38; onClicked: library.rate(index) } }
                    ComboBox { objectName: "library.flagChoice"; Layout.minimumWidth: 0; Layout.maximumWidth: 120; model: [qsTranslate("Luma", "Flag…"), qsTranslate("Luma", "Pick"), qsTranslate("Luma", "Reject"), qsTranslate("Luma", "Unflagged")]; onActivated: { if (currentIndex > 0) library.flag([0,1,-1,0][currentIndex]); currentIndex = 0 } }
                    ComboBox { objectName: "library.labelChoice"; Layout.minimumWidth: 0; Layout.maximumWidth: 120; model: [qsTranslate("Luma", "Label…"), qsTranslate("Luma", "No label"), qsTranslate("Luma", "Red"), qsTranslate("Luma", "Yellow"), qsTranslate("Luma", "Green"), qsTranslate("Luma", "Blue"), qsTranslate("Luma", "Purple")]; onActivated: { if (currentIndex > 0) library.label(["","","red","yellow","green","blue","purple"][currentIndex]); currentIndex = 0 } }
                }
                RowLayout {
                    Layout.minimumWidth: 0; Layout.fillWidth: true; spacing: 6
                    ComboBox { id: targetCollection; Layout.minimumWidth: 0; model: library.collections.filter(c=>!c.smart); textRole: "name"; Layout.maximumWidth: 145 }
                    Button { objectName: "libraryAddToCollection"; implicitWidth: 36; text: "+"; enabled: targetCollection.currentIndex >= 0 && targetCollection.count > 0; onClicked: library.collectionMembership(targetCollection.model[targetCollection.currentIndex].id,true); Accessible.name: qsTranslate("Luma", "Add to collection") }
                    Button { objectName: "libraryRemoveFromCollection"; implicitWidth: 36; text: "−"; enabled: !!pane.currentCollection && library.collections.some(c=>c.id===pane.currentCollection&&!c.smart); onClicked: library.collectionMembership(pane.currentCollection,false); Accessible.name: qsTranslate("Luma", "Remove from collection") }
                }
            }
            Label { text: qsTranslate("Luma", library.status); font.pixelSize: 10; color: "#8290a1"; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }
}
