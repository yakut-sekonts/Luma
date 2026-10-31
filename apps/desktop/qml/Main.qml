import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "main.window"
    visible: true
    width: 1320
    height: 840
    minimumWidth: 1000
    minimumHeight: 640
    title: "Apertone " + Qt.application.version + (editor.hasPhoto ? " — " + editor.filename : "")
    color: Theme.window
    palette.window: Theme.window
    palette.base: Theme.panel
    palette.alternateBase: Theme.panelRaised
    palette.text: Theme.text
    palette.windowText: Theme.text
    palette.placeholderText: Theme.faint
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.dark ? Theme.text : "#ffffff"
    palette.button: Theme.control
    palette.mid: Theme.divider
    palette.dark: Theme.divider
    palette.light: Theme.panelRaised
    font.family: "Avenir Next"
    font.pixelSize: 12
    Binding { target: Theme; property: "dark"; value: appearance.dark }
    onLibraryModeChanged: {if(libraryMode) { editor.cancelWhiteBalance(); editor.cancelCreative(); editor.cancelAI(); editor.retouchMode="off"; editor.localMode=false; editor.cropMode=false }}
    property bool libraryMode: true
    VersionsDialog { id: processingVersions }
    HistoryDialog { id: processingHistory }
    function ensureActiveFocusVisible(scroll) {
        const item = root.activeFocusItem
        const view = scroll ? scroll.contentItem : null
        if (!scroll || !scroll.visible || !item || !view || !view.contentItem)
            return
        let ancestor = item
        while (ancestor && ancestor !== view.contentItem)
            ancestor = ancestor.parent
        if (!ancestor)
            return
        const position = item.mapToItem(view, 0, 0)
        let target = view.contentY
        if (position.y < 0)
            target += position.y
        else if (position.y + item.height > view.height)
            target += position.y + item.height - view.height
        const minimum = view.originY
        const maximum = minimum + Math.max(0, view.contentHeight - view.height)
        view.cancelFlick()
        view.contentY = Math.max(minimum, Math.min(maximum, target))
    }
    function revealActiveFocus() {
        ensureActiveFocusVisible(presetsScroll)
        ensureActiveFocusVisible(adjustmentsScroll)
        ensureActiveFocusVisible(exportSettings.contentItem)
        ensureActiveFocusVisible(presetSettings.editorScrollView)
        ensureActiveFocusVisible(processingHistory.editorScrollView)
    }
    onActiveFocusItemChanged: Qt.callLater(root.revealActiveFocus)
    Connections {
        target: updates
        function onChanged() {
            if (updates.restartPending) restartWait.open();
            else restartWait.close();
        }
    }
    Dialog {
        id: restartWait; objectName: "restart.wait"
        title: qsTranslate("Apertone", "Preparing update")
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 430
        ColumnLayout {
            width: parent.width
            Label { text: qsTranslate("Apertone", updates.message); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { visible: editor.error.length > 0; text: qsTranslate("Apertone", editor.error); color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ApertoneButton { visible: editor.saving && !editor.busy; text: qsTranslate("Apertone", "Retry save"); onClicked: editor.retrySave() }
            ApertoneButton { visible: editor.exporting; text: qsTranslate("Apertone", "Cancel export"); onClicked: editor.cancelExport() }
            ApertoneButton { objectName: "restart.cancelImport"; visible: library.importing; text: qsTranslate("Apertone", "Cancel import"); onClicked: library.cancelImport() }
        }
    }
    Connections { target: batch; function onChanged() { if (batch.working) batchWait.open(); else batchWait.close() } }
    Dialog {
        id: batchWait; objectName: "batch.wait"; title: qsTranslate("Apertone", "Batch processing"); modal: true
        closePolicy: Popup.NoAutoClose; anchors.centerIn: parent; width: 450; implicitWidth: 450
        ColumnLayout {
            width: parent.width
            Label { text: qsTranslate("Apertone", batch.status); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ProgressBar { value: batch.progress; indeterminate: batch.waitingForSave; Layout.fillWidth: true }
            Label { text: qsTranslate("Apertone", editor.error); textFormat: Text.PlainText; visible: batch.waitingForSave && text.length > 0; color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ApertoneButton { text: qsTranslate("Apertone", "Retry save"); visible: batch.waitingForSave && editor.saving && !editor.busy; onClicked: editor.retrySave() }
            ApertoneButton { objectName: "batch.cancel"; text: qsTranslate("Apertone", "Cancel"); onClicked: batch.cancel() }
        }
    }
    onClosing: function(close) { const d = exporter.requestClose(); const c = batch.requestClose(); const b = editor.requestClose(); const a = d && c && b ? library.requestClose() : false; close.accepted = a && b && c && d }
    Connections { target: exporter; function onCloseReady() { root.close() } }
    Connections { target: batch; function onCloseReady() { root.close() } }
    Connections { target: library; function onCloseReady() { root.close() } }
    Connections { target: editor; function onPhotoLoaded(id) { root.libraryMode = false } }
    Connections { target: editor; function onCloseReady() { root.close() } }
    Shortcut { enabled: !updates.restartPending && !batch.working; sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !editor.busy && !updates.restartPending && !batch.working && (processingHistory.visible ? editor.canUndo : root.libraryMode ? batch.canUndo : editor.canUndo || batch.canUndo); onActivated: !processingHistory.visible && (root.libraryMode || !editor.canUndo) ? batch.undo() : editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !editor.busy && !updates.restartPending && !batch.working && (processingHistory.visible ? editor.canRedo : root.libraryMode ? batch.canRedo : editor.canRedo || batch.canRedo); onActivated: !processingHistory.visible && (root.libraryMode || !editor.canRedo) ? batch.redo() : editor.redo() }
    Shortcut { enabled: !updates.restartPending; context: Qt.ApplicationShortcut; sequences: [StandardKey.Quit]; onActivated: root.close() }
    Shortcut { enabled: editor.aiBusy || editor.aiObjectPicking || editor.retouchBusy || editor.retouchState.drawing || editor.whiteBalanceBusy || editor.whiteBalancePicking || editor.creativeBusy || editor.pointColorPicking; sequence: "Escape"; onActivated: { editor.cancelAI(); editor.cancelRetouch(); editor.cancelWhiteBalance(); editor.cancelCreative() } }
    Shortcut { enabled: editor.hasPhoto && !editor.busy && !updates.restartPending && !batch.working; sequence: "\\"; onActivated: editor.original = !editor.original }
    Shortcut { enabled: !updates.restartPending && !batch.working; sequence: "Ctrl+Shift+F"; onActivated: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut {
        sequences: ["Return", "Enter"]
        context: Qt.ApplicationShortcut
        autoRepeat: false
        enabled: root.active && root.activeFocusItem instanceof Button && root.activeFocusItem.visible && root.activeFocusItem.enabled
        onActivated: root.activeFocusItem.click()
    }
    FileDialog {
        id: fileDialog
        objectName: "main.openDialog"
        title: qsTranslate("Apertone", "Open photo")
        nameFilters: [qsTranslate("Apertone", "Photos (*.3fr *.3FR *.arw *.ARW *.cr2 *.CR2 *.cr3 *.CR3 *.crw *.CRW *.dcr *.DCR *.dcs *.DCS *.dng *.DNG *.erf *.ERF *.fff *.FFF *.gpr *.GPR *.iiq *.IIQ *.k25 *.K25 *.kdc *.KDC *.mef *.MEF *.mos *.MOS *.mrw *.MRW *.nef *.NEF *.nrw *.NRW *.orf *.ORF *.pef *.PEF *.ptx *.PTX *.raf *.RAF *.raw *.RAW *.rw2 *.RW2 *.rwl *.RWL *.sr2 *.SR2 *.srf *.SRF *.srw *.SRW *.jpg *.JPG *.jpeg *.JPEG *.png *.PNG *.tif *.TIF *.tiff *.TIFF *.heic *.HEIC *.heif *.HEIF *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.avif *.AVIF)"), qsTranslate("Apertone", "All files (*)")]
        onAccepted: editor.openFile(selectedFile)
    }
    PreferencesPanel { id: preferences }
    ExportPanel { id: exportSettings }
    DropArea {
        enabled: !updates.restartPending && !batch.working
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length === 1) { editor.openFile(drop.urls[0]); drop.acceptProposedAction() }
        }
    }
    ColumnLayout {
        enabled: !updates.restartPending && !batch.working
        anchors.fill: parent
        spacing: 0
        Rectangle {
            objectName: "main.toolbar"
            Layout.fillWidth: true; Layout.preferredHeight: Theme.toolbarHeight; color: Theme.panel
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: root.width < 1200 ? 6 : 10
                Item {
                    implicitWidth: 19; implicitHeight: 19
                    Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.width: 1; border.color: Theme.text }
                    Rectangle { width: 7; height: 13; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; radius: 4; color: Theme.accent }
                }
                Label { text: "Apertone"; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                ApertoneButton { objectName: "main.libraryButton"; text: qsTranslate("Apertone", "Library"); highlighted: root.libraryMode; onClicked: root.libraryMode = true }
                ApertoneButton { objectName: "main.developButton"; text: qsTranslate("Apertone", "Develop"); highlighted: !root.libraryMode; onClicked: root.libraryMode = false }
                Label { objectName: "main.filename"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: editor.filename; color: Theme.muted; font.pixelSize: 11; elide: Text.ElideMiddle; horizontalAlignment: Text.AlignHCenter }
                ApertoneButton { objectName: "settingsButton"; text: qsTranslate("Apertone", "Settings"); Accessible.name: text; onClicked: preferences.open() }
                UpdateBell {}
                ApertoneButton { objectName: "main.undoButton"; text: qsTranslate("Apertone", "Undo"); enabled: !editor.busy && !batch.working && (root.libraryMode ? batch.canUndo : editor.canUndo || batch.canUndo); onClicked: root.libraryMode || !editor.canUndo ? batch.undo() : editor.undo(); Accessible.name: text }
                ApertoneButton { objectName: "main.redoButton"; text: qsTranslate("Apertone", "Redo"); enabled: !editor.busy && !batch.working && (root.libraryMode ? batch.canRedo : editor.canRedo || batch.canRedo); onClicked: root.libraryMode || !editor.canRedo ? batch.redo() : editor.redo(); Accessible.name: text }
                ApertoneButton { objectName: "exportButton"; Layout.minimumWidth: 0; Layout.maximumWidth: 140; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Apertone", "Export"); enabled: (editor.hasPhoto || library.selectedCount > 0) && !editor.busy && !editor.exporting; onClicked: exportSettings.open() }
                ApertoneButton { objectName: "openPhotoButton"; Layout.minimumWidth: 0; Layout.maximumWidth: 180; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Apertone", "+  Open photo"); highlighted: true; enabled: !editor.busy; onClicked: fileDialog.open() }
            }
        }
        Rectangle {
            visible: editor.error.length > 0
            Layout.fillWidth: true; Layout.preferredHeight: visible ? errorLayout.implicitHeight + 24 : 0; color: Theme.errorSurface
            RowLayout {
                id: errorLayout; anchors.fill: parent; anchors.margins: 12
                Label { objectName: "main.editorError"; text: qsTranslate("Apertone", editor.error); color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                ApertoneButton { objectName: "editor.retrySave"; visible: editor.saving && !editor.busy; text: qsTranslate("Apertone", "Retry save"); onClicked: editor.retrySave() }
            }
        }
        RowLayout {
            visible: editor.exporting
            Layout.fillWidth: true; Layout.margins: visible ? 10 : 0
            Label { text: qsTranslate("Apertone", "Export"); color: Theme.accent }
            ProgressBar { value: editor.exportProgress; Layout.fillWidth: true }
            ApertoneButton { text: qsTranslate("Apertone", "Cancel"); onClicked: editor.cancelExport() }
        }
        ColumnLayout {
            visible: exporter.working || exporter.status.length > 0 || exporter.error.length > 0
            Layout.fillWidth: true; Layout.margins: 10
            RowLayout {
                Label { text: qsTranslate("Apertone", exporter.status); textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: exporter.completed + " / " + exporter.total }
                ApertoneButton { objectName: "export.pause"; visible: exporter.working && !exporter.paused; text: exporter.pausePending ? qsTranslate("Apertone", "Pause after this file…") : qsTranslate("Apertone", "Pause"); enabled: !exporter.pausePending; onClicked: exporter.pause() }
                ApertoneButton { objectName: "export.resume"; visible: exporter.paused; text: qsTranslate("Apertone", "Resume"); onClicked: exporter.resume() }
                ApertoneButton { objectName: "export.cancel"; visible: exporter.working; text: qsTranslate("Apertone", "Cancel"); onClicked: exporter.cancel() }
                ApertoneButton { text: qsTranslate("Apertone", "Settings / errors"); onClicked: exportSettings.open() }
            }
            Label { objectName: "export.currentFilename"; visible: exporter.working; text: exporter.currentFile; textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true }
            ProgressBar { objectName: "export.progress"; visible: exporter.working; value: exporter.progress; indeterminate: exporter.waitingForSave; Layout.fillWidth: true }
            Label { visible: exporter.error.length > 0; text: qsTranslate("Apertone", exporter.error); textFormat: Text.PlainText; color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ScrollView {
                objectName: "export.failuresScroll"
                visible: exporter.failures.length > 0
                Layout.fillWidth: true; Layout.preferredHeight: 65
                TextArea { objectName: "export.failures"; readOnly: true; text: exporter.failures.join("\n"); textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: Theme.negative }
            }
            ApertoneButton { objectName: "export.retrySave"; visible: exporter.waitingForSave && editor.saving && !editor.busy; text: qsTranslate("Apertone", "Retry save"); onClicked: editor.retrySave() }
        }
        LibraryPane { objectName: "library.pane"; visible: root.libraryMode; Layout.fillWidth: true; Layout.fillHeight: true }
        SplitView {
            visible: !root.libraryMode
            id: panels
            objectName: "develop.panels"
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 1; color: Theme.divider }
            Rectangle {
                objectName: "develop.presetsPane"
                SplitView.preferredWidth: Theme.sidebarWidth; SplitView.minimumWidth: 170; SplitView.maximumWidth: 320; color: Theme.panel
                ScrollView {
                    id: presetsScroll
                    objectName: "develop.presetsScroll"
                    Component.onCompleted: ScrollBar.vertical.objectName = "develop.presetsBar"
                    anchors.fill: parent
                    anchors.margins: Theme.panelPadding
                    clip: true
                    rightPadding: effectiveScrollBarWidth
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: presetsScroll.availableWidth
                        height: Math.max(implicitHeight, presetsScroll.availableHeight)
                        spacing: 12
                        Label { text: qsTranslate("Apertone", "WORKSPACE"); color: Theme.faint; font.pixelSize: 9; font.letterSpacing: 1 }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 30; radius: Theme.radius; color: Theme.accentSoft
                            Label { anchors.centerIn: parent; text: qsTranslate("Apertone", "Photo development"); color: Theme.accent; font.pixelSize: 12 }
                        }
                        Label { text: qsTranslate("Apertone", "SOURCE PHOTO"); color: Theme.faint; font.pixelSize: 9; font.letterSpacing: 1; Layout.topMargin: 10 }
                        Label { objectName: "develop.sourceFilename"; text: editor.hasPhoto ? editor.filename : qsTranslate("Apertone", "No photo selected"); color: Theme.text; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: editor.dimensions; color: Theme.faint; font.pixelSize: 11 }
                        MetadataRow { objectName: "develop.decode"; visible: editor.hasPhoto; label: qsTranslate("Apertone", "Decode"); value: editor.decoderSummary; Layout.fillWidth: true }
                        Label { text: qsTranslate("Apertone", "Original unchanged"); color: Theme.muted; font.pixelSize: 10; visible: editor.hasPhoto }
                        ApertoneButton {
                            objectName: "develop.history"
                            text: qsTranslate("Apertone", "History")
                            enabled: editor.hasPhoto && !editor.busy && !batch.working && !exporter.working && !updates.restartPending
                            Layout.fillWidth: true
                            onClicked: processingHistory.open()
                        }
                        ApertoneButton {
                            objectName: "develop.versions"
                            text: qsTranslate("Apertone", "Processing versions")
                            enabled: editor.hasPhoto && !editor.busy && !batch.working && !exporter.working && !updates.restartPending
                            Layout.fillWidth: true
                            onClicked: processingVersions.open()
                        }
                        PresetPanel { id: presetSettings; libraryModel: library; Layout.fillWidth: true; Layout.fillHeight: true }
                        Label { text: qsTranslate("Apertone", "CATALOG"); color: Theme.faint; font.pixelSize: 9; font.letterSpacing: 1 }
                        Label { text: editor.catalogPath; color: Theme.faint; font.pixelSize: 10; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                        Label { text: "APERTONE / DEVELOP CORE"; color: Theme.accent; font.pixelSize: 9; font.letterSpacing: 2 }
                    }
                }
            }
            Rectangle {
                objectName: "develop.canvasPane"
                SplitView.fillWidth: true; SplitView.minimumWidth: 360; color: Theme.canvas
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10; spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight; text: editor.detailDiagnostic.length ? qsTranslate("Apertone", editor.detailDiagnostic) : editor.original ? qsTranslate("Apertone", "ORIGINAL PHOTO") : editor.presetPreview ? qsTranslate("Apertone", "PRESET PREVIEW") : qsTranslate("Apertone", "PREVIEW"); color: Theme.canvasMuted; font.pixelSize: 9; font.letterSpacing: 2 }
                        ApertoneButton {
                            objectName: "develop.fit"
                            text: qsTranslate("Apertone", "FIT")
                            checkable: true; checked: editor.zoomMode === "fit"
                            enabled: editor.hasPhoto && !editor.busy
                            onClicked: editor.zoomMode = "fit"
                        }
                        ApertoneButton {
                            objectName: "develop.fill"
                            text: qsTranslate("Apertone", "FILL")
                            checkable: true; checked: editor.zoomMode === "fill"
                            enabled: editor.zoomAvailable
                            onClicked: editor.zoomMode = "fill"
                        }
                        ApertoneTextField {
                            id: zoomPercent
                            objectName: "develop.zoomPercent"
                            Layout.preferredWidth: 64; Layout.minimumWidth: 64
                            enabled: editor.zoomAvailable
                            horizontalAlignment: Text.AlignRight
                            Accessible.name: qsTranslate("Apertone", "Zoom") + " %"
                            property bool modified: false
                            validator: DoubleValidator { bottom: editor.minimumZoomFactor * 100; top: editor.maximumZoomFactor * 100; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
                            Binding {
                                target: zoomPercent; property: "text"
                                value: Number((previewViewport.effectiveZoom * 100).toFixed(2)).toString()
                                when: !zoomPercent.activeFocus
                            }
                            function commitZoom() {
                                if (modified && acceptableInput) previewViewport.requestScale(Number(text) / 100)
                                modified = false
                            }
                            onTextEdited: modified = true
                            onAccepted: { commitZoom(); previewViewport.forceActiveFocus() }
                            onEditingFinished: commitZoom()
                            Keys.onEscapePressed: { modified = false; previewViewport.forceActiveFocus() }
                        }
                        Label { text: "%"; color: Theme.canvasMuted }
                        ApertoneButton {
                            objectName: "develop.actualPixels"
                            text: "100%"
                            Accessible.name: qsTranslate("Apertone", "Zoom") + " 100%"
                            checkable: true; checked: editor.oneToOne
                            enabled: editor.zoomAvailable
                            onClicked: editor.oneToOne = true
                        }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Layout.minimumWidth: 84; Layout.minimumHeight: 84
                        PhotoViewport {
                            id: previewViewport
                            objectName: "develop.viewport"
                            controller: editor
                            anchors.fill: parent
                        Image {
                            id: previewImage
                            objectName: "develop.preview"
                            width: previewViewport.imageItemWidth
                            height: previewViewport.imageItemHeight
                            x: (previewViewport.contentWidth - width) / 2
                            y: (previewViewport.contentHeight - height) / 2
                            source: editor.previewUrl; fillMode: Image.PreserveAspectFit; cache: false; visible: editor.hasPhoto && !editor.busy
                            horizontalAlignment: Image.AlignHCenter
                            verticalAlignment: Image.AlignVCenter
                            MouseArea {
                                anchors.fill: parent
                                enabled: editor.aiObjectPicking && !editor.busy
                                z: 1
                                cursorShape: Qt.CrossCursor
                                onClicked: function(mouse) {
                                    const left = (width - previewImage.paintedWidth) / 2
                                    const top = (height - previewImage.paintedHeight) / 2
                                    if (previewImage.paintedWidth > 0 && previewImage.paintedHeight > 0 &&
                                        mouse.x >= left && mouse.x <= left + previewImage.paintedWidth &&
                                        mouse.y >= top && mouse.y <= top + previewImage.paintedHeight)
                                        editor.pickAIMask((mouse.x - left) / previewImage.paintedWidth,
                                                          (mouse.y - top) / previewImage.paintedHeight)
                                }
                            }
                            MouseArea {
                                objectName: "develop.whiteBalance.sampleArea"
                                anchors.fill: parent
                                enabled: editor.whiteBalancePicking && editor.hasPhoto && !editor.busy
                                z: 2
                                cursorShape: editor.whiteBalanceCanvasReady ? Qt.CrossCursor : Qt.WaitCursor
                                onClicked: function(mouse) {
                                    if (!editor.whiteBalanceCanvasReady || previewImage.status !== Image.Ready) return
                                    const left = (width - previewImage.paintedWidth) / 2
                                    const top = (height - previewImage.paintedHeight) / 2
                                    if (previewImage.paintedWidth > 0 && previewImage.paintedHeight > 0 &&
                                        mouse.x >= left && mouse.x <= left + previewImage.paintedWidth &&
                                        mouse.y >= top && mouse.y <= top + previewImage.paintedHeight)
                                        editor.sampleWhiteBalance((mouse.x - left) / previewImage.paintedWidth,
                                                                  (mouse.y - top) / previewImage.paintedHeight)
                                }
                            }
                            MouseArea {
                                objectName: "develop.pointColor.sampleArea"
                                anchors.fill: parent; z: 3
                                enabled: editor.pointColorPicking && editor.hasPhoto && !editor.busy
                                cursorShape: editor.pointColorCanvasReady ? Qt.CrossCursor : Qt.WaitCursor
                                onClicked: function(mouse) {
                                    if (!editor.pointColorCanvasReady || previewImage.status !== Image.Ready) return
                                    const left=(width-previewImage.paintedWidth)/2, top=(height-previewImage.paintedHeight)/2
                                    if(previewImage.paintedWidth>0 && previewImage.paintedHeight>0 && mouse.x>=left && mouse.y>=top &&
                                       mouse.x<=left+previewImage.paintedWidth && mouse.y<=top+previewImage.paintedHeight)
                                        editor.samplePointColor((mouse.x-left)/previewImage.paintedWidth,(mouse.y-top)/previewImage.paintedHeight)
                                }
                            }
                            MouseArea {
                                objectName: "develop.targetedArea"
                                anchors.fill: parent
                                enabled: !editor.cropMode && !editor.guidedMode && editor.targetedTool !== "none" && editor.hasPhoto && !editor.busy && !editor.original
                                cursorShape: Qt.SizeVerCursor
                                property real startY: 0
                                property bool adjusting: false
                                onPressed: function(mouse) {
                                    const left=(width-previewImage.paintedWidth)/2, top=(height-previewImage.paintedHeight)/2
                                    if(previewImage.paintedWidth<=0||previewImage.paintedHeight<=0||mouse.x<left||mouse.x>left+previewImage.paintedWidth||mouse.y<top||mouse.y>top+previewImage.paintedHeight){mouse.accepted=false;return}
                                    startY=mouse.y;adjusting=editor.beginTargeted((mouse.x-left)/previewImage.paintedWidth,(mouse.y-top)/previewImage.paintedHeight)
                                }
                                onPositionChanged: function(mouse) { if(pressed&&adjusting&&previewImage.paintedHeight>0)editor.adjustTargeted((startY-mouse.y)/previewImage.paintedHeight) }
                                onReleased: { adjusting=false;editor.endTargeted() }
                                onCanceled: { adjusting=false;editor.endTargeted() }
                            }
                        }
                        RetouchOverlay {
                            x: (previewImage.width-previewImage.paintedWidth)/2
                            y: (previewImage.height-previewImage.paintedHeight)/2
                            // These tools currently operate in Fit. Do not allocate
                            // hidden Canvas backing images at magnified dimensions.
                            width: previewViewport.pixelMode ? 0 : previewImage.paintedWidth
                            height: previewViewport.pixelMode ? 0 : previewImage.paintedHeight
                        }
                        LocalOverlay {
                            photoWidth: editor.localMode || !previewViewport.pixelMode ? previewImage.paintedWidth : 0
                            photoHeight: editor.localMode || !previewViewport.pixelMode ? previewImage.paintedHeight : 0
                            width: Math.max(84, photoWidth); height: Math.max(84, photoHeight)
                            x: previewImage.x + (previewImage.width-width)/2
                            y: previewImage.y + (previewImage.height-height)/2
                            paintBounds: previewViewport.visibleBounds(x,y,width,height)
                        }
                        TransformOverlay {
                            x: previewImage.x + (previewImage.width-previewImage.paintedWidth)/2
                            y: previewImage.y + (previewImage.height-previewImage.paintedHeight)/2
                            width: editor.guidedMode || !previewViewport.pixelMode ? previewImage.paintedWidth : 0
                            height: editor.guidedMode || !previewViewport.pixelMode ? previewImage.paintedHeight : 0
                            paintBounds: previewViewport.visibleBounds(x,y,width,height)
                        }
                        CropOverlay {
                            x: previewImage.x + (previewImage.width-previewImage.paintedWidth)/2
                            y: previewImage.y + (previewImage.height-previewImage.paintedHeight)/2
                            width: editor.cropMode || !previewViewport.pixelMode ? previewImage.paintedWidth : 0
                            height: editor.cropMode || !previewViewport.pixelMode ? previewImage.paintedHeight : 0
                            paintBounds: previewViewport.visibleBounds(x,y,width,height)
                        }
                        }
                        ColumnLayout {
                            anchors.centerIn: parent; spacing: 12; visible: !editor.hasPhoto && !editor.busy
                            Item {
                                Layout.preferredWidth: 44; Layout.preferredHeight: 44; Layout.alignment: Qt.AlignHCenter
                                Rectangle { anchors.fill: parent; radius: width / 2; color: "transparent"; border.width: 2; border.color: Theme.canvasText }
                                Rectangle { width: 16; height: 30; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; radius: 8; color: Theme.accent }
                            }
                            Label { text: qsTranslate("Apertone", "It starts with a photo."); color: Theme.canvasText; font.pixelSize: 22; font.weight: Font.Light; Layout.alignment: Qt.AlignHCenter }
                            Label { text: qsTranslate("Apertone", "Open a photo. Find your light."); color: Theme.canvasMuted; font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                            ApertoneButton { text: qsTranslate("Apertone", "Open photo"); Layout.alignment: Qt.AlignHCenter; onClicked: fileDialog.open() }
                            Label { text: qsTranslate("Apertone", "RAW and common images"); color: Theme.canvasMuted; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                        }
                        ColumnLayout {
                            anchors.centerIn: parent; visible: editor.busy; spacing: 16; width: Math.min(parent.width - 60, 300)
                            BusyIndicator { running: editor.busy; Layout.alignment: Qt.AlignHCenter }
                            Label { text: qsTranslate("Apertone", "Decoding image pixels"); color: Theme.canvasText; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                            ProgressBar { value: editor.progress; Layout.fillWidth: true }
                            ApertoneButton { objectName: "editor.cancelLoad"; text: qsTranslate("Apertone", "Cancel"); onClicked: editor.cancelLoad(); Layout.alignment: Qt.AlignHCenter }
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter; spacing: 8
                        ApertoneButton { objectName: "develop.processedButton"; text: qsTranslate("Apertone", "Develop"); checkable: true; checked: !editor.original; enabled: editor.hasPhoto && !editor.busy; onClicked: editor.original = false }
                        ApertoneButton { objectName: "develop.originalButton"; text: qsTranslate("Apertone", "Original"); checkable: true; checked: editor.original; enabled: editor.hasPhoto && !editor.busy; onClicked: editor.original = true }
                    }
                    Label {
                        visible: editor.whiteBalancePicking
                        text: qsTranslate("Apertone", "Click a neutral gray area. Escape cancels the eyedropper.")
                        color: Theme.accent; font.pixelSize: 11
                        wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                    }
                    Label { visible:editor.targetedTool!=="none";text:qsTranslate("Apertone", "Click a tone or color and drag up / down");color:Theme.accent;font.pixelSize:11;Layout.alignment:Qt.AlignHCenter }
                }
            }
            Rectangle {
                objectName: "develop.adjustmentsPane"
                SplitView.preferredWidth: Theme.inspectorWidth; SplitView.minimumWidth: 250; SplitView.maximumWidth: 380; color: Theme.panel
                ScrollView {
                    id: adjustmentsScroll
                    objectName: "develop.adjustmentsScroll"
                    Component.onCompleted: ScrollBar.vertical.objectName = "develop.adjustmentsBar"
                    anchors.fill: parent
                    anchors.margins: Theme.panelPadding
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: adjustmentsScroll.availableWidth
                        spacing: 9
                        HistogramPanel { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: adjustmentsScroll.availableWidth }
                        LocalPanel { Layout.fillWidth: true }
                        CreativePanel { Layout.fillWidth: true }
                        Label { text: qsTranslate("Apertone", "Adjustments"); font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text }
                        RowLayout {
                            CheckBox { objectName: "develop.whiteBalance.enabled"; Accessible.name: qsTranslate("Apertone", "Enable white balance"); checked: editor.correctionStates.whiteBalance; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("whiteBalance",checked); padding: 0 }
                            Label { text: qsTranslate("Apertone", "WHITE BALANCE"); color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1 }
                        }
                        ApertoneComboBox {
                            objectName: "develop.whiteBalance.preset"
                            Layout.fillWidth: true
                            textRole: "label"
                            model: [{ value: "As Shot", label: qsTranslate("Apertone", "As Shot") },{ value: "Daylight", label: qsTranslate("Apertone", "Daylight") },{ value: "Cloudy", label: qsTranslate("Apertone", "Cloudy") },{ value: "Shade", label: qsTranslate("Apertone", "Shade") },{ value: "Tungsten", label: qsTranslate("Apertone", "Tungsten") },{ value: "Fluorescent", label: qsTranslate("Apertone", "Fluorescent") },{ value: "Custom", label: qsTranslate("Apertone", "Custom") }]
                            currentIndex: editor.customWhiteBalance ? 6 : 0
                            enabled: editor.hasPhoto && !editor.busy && editor.correctionStates.whiteBalance
                            onActivated: editor.setWhiteBalancePreset(model[currentIndex].value)
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ApertoneButton {
                                objectName: "develop.whiteBalance.auto"
                                text: qsTranslate("Apertone", "Auto")
                                Accessible.name: qsTranslate("Apertone", "Auto white balance")
                                Layout.fillWidth: true
                                enabled: editor.hasPhoto && !editor.busy && !editor.original &&
                                         !editor.exporting && !editor.whiteBalanceBusy && editor.correctionStates.whiteBalance
                                onClicked: editor.autoWhiteBalance()
                                ToolTip.visible: hovered
                                ToolTip.text: qsTranslate("Apertone", "Estimates white balance from the visible crop. Strongly colored scenes may need manual adjustment.")
                            }
                            ApertoneButton {
                                objectName: "develop.whiteBalance.eyedropper"
                                text: qsTranslate("Apertone", "Eyedropper")
                                Accessible.name: qsTranslate("Apertone", "White balance eyedropper")
                                Layout.fillWidth: true
                                checkable: true; checked: editor.whiteBalancePicking
                                enabled: editor.hasPhoto && !editor.busy && !editor.original &&
                                         !editor.exporting && !editor.whiteBalanceBusy && editor.correctionStates.whiteBalance
                                onClicked: editor.whiteBalancePicking = !editor.whiteBalancePicking
                            }
                        }
                        RowLayout {
                            visible: editor.whiteBalanceBusy || editor.whiteBalancePicking
                            Layout.fillWidth: true
                            BusyIndicator { running: editor.whiteBalanceBusy; visible: running; implicitWidth: 22; implicitHeight: 22 }
                            Label {
                                text: editor.whiteBalanceBusy ? qsTranslate("Apertone", "Estimating white balance…") : qsTranslate("Apertone", "Select a neutral area")
                                Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: Theme.muted
                            }
                            ApertoneButton { text: qsTranslate("Apertone", "Cancel"); onClicked: editor.cancelWhiteBalance() }
                        }
                        Label {
                            objectName: "develop.whiteBalance.message"
                            visible: text.length > 0; text: editor.whiteBalanceMessage
                            Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11; color: Theme.accent
                        }
                        DevelopSlider { parameter: "temperature"; label: qsTranslate("Apertone", "Temperature"); minimum: 2000; maximum: 25000; step: 50; defaultValue: 6500; suffix: " K"; correction: "whiteBalance"; allowDisable: false; Layout.fillWidth: true }
                        DevelopSlider { parameter: "tint"; label: qsTranslate("Apertone", "Tint"); minimum: -150; maximum: 150; correction: "whiteBalance"; allowDisable: false; Layout.fillWidth: true }
                        Label { visible: !editor.customWhiteBalance; text: qsTranslate("Apertone", "As Shot keeps the source white balance. Kelvin is available in Custom."); font.pixelSize: 10; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        Label { text: qsTranslate("Apertone", "LIGHT"); color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 2 }
                        DevelopSlider { parameter: "exposure"; label: qsTranslate("Apertone", "Exposure"); minimum: -5; maximum: 5; step: 0.01; suffix: " EV"; Layout.fillWidth: true }
                        DevelopSlider { parameter: "contrast"; label: qsTranslate("Apertone", "Contrast"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "highlights"; label: qsTranslate("Apertone", "Highlights"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "shadows"; label: qsTranslate("Apertone", "Shadows"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "whites"; label: qsTranslate("Apertone", "Whites"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "blacks"; label: qsTranslate("Apertone", "Blacks"); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        Label { text: qsTranslate("Apertone", "COLOR"); color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 2 }
                        DevelopSlider { parameter: "vibrance"; label: qsTranslate("Apertone", "Vibrance"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "saturation"; label: qsTranslate("Apertone", "Saturation"); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:Theme.divider }
                        CurvesPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:Theme.divider }
                        HSLPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:Theme.divider }
                        CropPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        TransformPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        LensPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        DetailPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        RetouchPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        ApertoneButton { objectName: "develop.resetButton"; text: qsTranslate("Apertone", "Reset all edits"); enabled: editor.hasPhoto && !editor.busy; onClicked: editor.resetEdits(); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.divider }
                        Label { text: qsTranslate("Apertone", "CAPTURE DATA"); color: Theme.muted; font.pixelSize: 9; font.letterSpacing: 1.5 }
                        MetadataRow { objectName: "develop.metadata.camera"; label: qsTranslate("Apertone", "Camera"); value: editor.camera; Layout.fillWidth: true }
                        MetadataRow { objectName: "develop.metadata.lens"; label: qsTranslate("Apertone", "Lens"); value: { const language = Qt.uiLanguage; return editor.lens } Layout.fillWidth: true }
                        MetadataRow { objectName: "develop.metadata.iso"; label: "ISO"; value: editor.iso; Layout.fillWidth: true }
                        MetadataRow { objectName: "develop.metadata.shutter"; label: qsTranslate("Apertone", "Shutter speed"); value: editor.shutter; Layout.fillWidth: true }
                        MetadataRow { objectName: "develop.metadata.aperture"; label: qsTranslate("Apertone", "Aperture"); value: editor.aperture; Layout.fillWidth: true }
                        MetadataRow { objectName: "develop.metadata.focalLength"; label: qsTranslate("Apertone", "Focal length"); value: editor.focalLength; Layout.fillWidth: true }
                        Label { text: editor.saving ? qsTranslate("Apertone", "Saving changes…") : qsTranslate("Apertone", "Changes are saved automatically"); color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
            }
        }
        ListView {
            id: filmstrip
            objectName: "develop.filmstrip"
            visible: !root.libraryMode && (library.count > 0 || (library.loading && editor.hasPhoto))
            Layout.fillWidth: true; Layout.preferredHeight: visible ? Theme.filmstripHeight : 0
            orientation: ListView.Horizontal; clip: true; spacing: 6; model: library; cacheBuffer: 300
            function moveCurrentPhoto(direction, event) {
                event.accepted = event.modifiers === Qt.NoModifier
                if (!event.accepted) return
                if (direction > 0) incrementCurrentIndex()
                else decrementCurrentIndex()
                // Mouse clicks retain their viewport. Keyboard navigation must
                // explicitly reveal its item even after that positioning.
                if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            }
            Keys.onRightPressed: function(event) { moveCurrentPhoto(1, event) }
            Keys.onLeftPressed: function(event) { moveCurrentPhoto(-1, event) }
            delegate: PhotoCard { width: 128; height: 104; compact: true }
            ScrollBar.horizontal: ScrollBar { objectName: "develop.filmstripBar" }
        }
        Rectangle {
            objectName: "main.footer"
            Layout.fillWidth: true; implicitHeight: Theme.footerHeight; color: Theme.panel
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                Label { objectName: "applicationVersion"; text: "Apertone " + Qt.application.version; font.pixelSize: 10; color: Theme.muted }
                Rectangle { width: 5; height: 5; radius: 3; color: editor.busy || editor.saving ? Theme.accent : Theme.positive }
                Label { objectName: "main.editorStatus"; text: qsTranslate("Apertone", editor.status); font.pixelSize: 10; color: Theme.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: qsTranslate("Apertone", "Edits do not change the original image"); color: Theme.faint; font.pixelSize: 10 }
            }
        }
    }
}
