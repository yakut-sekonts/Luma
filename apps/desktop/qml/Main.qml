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
    title: "Luma " + Qt.application.version + (editor.hasPhoto ? " — " + editor.filename : "")
    color: "#111318"
    palette.window: "#111318"
    palette.base: "#1b1e24"
    palette.text: "#e3e5e9"
    palette.windowText: "#e3e5e9"
    palette.placeholderText: "#858c97"
    palette.buttonText: "#e3e5e9"
    palette.highlight: "#dfae77"
    palette.button: "#252a32"
    font.family: "Helvetica Neue"
    onLibraryModeChanged: {if(libraryMode) { editor.localMode=false; editor.cropMode=false }}
    property bool libraryMode: true
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
        title: qsTranslate("Luma", "Preparing update")
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 430
        ColumnLayout {
            width: parent.width
            Label { text: qsTranslate("Luma", updates.message); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { visible: editor.error.length > 0; text: qsTranslate("Luma", editor.error); color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { visible: editor.saving && !editor.busy; text: qsTranslate("Luma", "Retry save"); onClicked: editor.retrySave() }
            Button { visible: editor.exporting; text: qsTranslate("Luma", "Cancel export"); onClicked: editor.cancelExport() }
            Button { objectName: "restart.cancelImport"; visible: library.importing; text: qsTranslate("Luma", "Cancel import"); onClicked: library.cancelImport() }
        }
    }
    Connections { target: batch; function onChanged() { if (batch.working) batchWait.open(); else batchWait.close() } }
    Dialog {
        id: batchWait; objectName: "batch.wait"; title: qsTranslate("Luma", "Batch processing"); modal: true
        closePolicy: Popup.NoAutoClose; anchors.centerIn: parent; width: 450; implicitWidth: 450
        ColumnLayout {
            width: parent.width
            Label { text: qsTranslate("Luma", batch.status); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ProgressBar { value: batch.progress; indeterminate: batch.waitingForSave; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", editor.error); textFormat: Text.PlainText; visible: batch.waitingForSave && text.length > 0; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Retry save"); visible: batch.waitingForSave && editor.saving && !editor.busy; onClicked: editor.retrySave() }
            Button { objectName: "batch.cancel"; text: qsTranslate("Luma", "Cancel"); onClicked: batch.cancel() }
        }
    }
    onClosing: function(close) { const d = exporter.requestClose(); const c = batch.requestClose(); const b = editor.requestClose(); const a = d && c && b ? library.requestClose() : false; close.accepted = a && b && c && d }
    Connections { target: exporter; function onCloseReady() { root.close() } }
    Connections { target: batch; function onCloseReady() { root.close() } }
    Connections { target: library; function onCloseReady() { root.close() } }
    Connections { target: editor; function onPhotoLoaded(id) { root.libraryMode = false } }
    Connections { target: editor; function onCloseReady() { root.close() } }
    Shortcut { enabled: !updates.restartPending && !batch.working; sequences: [StandardKey.Open]; onActivated: fileDialog.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !editor.busy && !updates.restartPending && !batch.working && (root.libraryMode ? batch.canUndo : editor.canUndo || batch.canUndo); onActivated: root.libraryMode || !editor.canUndo ? batch.undo() : editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !editor.busy && !updates.restartPending && !batch.working && (root.libraryMode ? batch.canRedo : editor.canRedo || batch.canRedo); onActivated: root.libraryMode || !editor.canRedo ? batch.redo() : editor.redo() }
    Shortcut { enabled: !updates.restartPending; context: Qt.ApplicationShortcut; sequences: [StandardKey.Quit]; onActivated: root.close() }
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
        title: qsTranslate("Luma", "Open photo")
        nameFilters: [qsTranslate("Luma", "Photos (*.nef *.NEF *.dng *.DNG *.cr2 *.CR2 *.cr3 *.CR3 *.arw *.ARW *.rw2 *.RW2 *.orf *.ORF *.raf *.RAF *.jpg *.JPG *.jpeg *.JPEG *.png *.PNG *.tif *.TIF *.tiff *.TIFF *.heic *.HEIC *.heif *.HEIF *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.avif *.AVIF)"), qsTranslate("Luma", "All files (*)")]
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
            Layout.fillWidth: true; Layout.preferredHeight: 64; color: "#191c22"
            RowLayout {
                anchors.fill: parent; anchors.margins: 20; spacing: root.width < 1200 ? 8 : 18
                Label { text: "◐"; color: "#dfae77"; font.pixelSize: 28 }
                Label { text: "luma"; color: "#f0f1f3"; font.pixelSize: 24; font.weight: Font.DemiBold; font.letterSpacing: 2 }
                Label { visible: root.width > 1450; text: qsTranslate("Luma", "PHOTO WORKSPACE"); color: "#747b88"; font.pixelSize: 10; font.letterSpacing: 2 }
                Button { objectName: "main.libraryButton"; text: qsTranslate("Luma", "Library"); highlighted: root.libraryMode; onClicked: root.libraryMode = true }
                Button { objectName: "main.developButton"; text: qsTranslate("Luma", "Develop"); highlighted: !root.libraryMode; onClicked: root.libraryMode = false }
                Item { Layout.fillWidth: true }
                Button { objectName: "settingsButton"; implicitWidth: 36; text: "⚙"; Accessible.name: qsTranslate("Luma", "Settings"); onClicked: preferences.open() }
                UpdateBell {}
                Label { objectName: "main.filename"; Layout.fillWidth: true; Layout.minimumWidth: 0; text: editor.filename; color: "#989fab"; font.pixelSize: 12; elide: Text.ElideMiddle; Layout.maximumWidth: 350 }
                Button { objectName: "main.undoButton"; text: "↶"; implicitWidth: 36; enabled: !editor.busy && !batch.working && (root.libraryMode ? batch.canUndo : editor.canUndo || batch.canUndo); onClicked: root.libraryMode || !editor.canUndo ? batch.undo() : editor.undo(); Accessible.name: qsTranslate("Luma", "Undo") }
                Button { objectName: "main.redoButton"; text: "↷"; implicitWidth: 36; enabled: !editor.busy && !batch.working && (root.libraryMode ? batch.canRedo : editor.canRedo || batch.canRedo); onClicked: root.libraryMode || !editor.canRedo ? batch.redo() : editor.redo(); Accessible.name: qsTranslate("Luma", "Redo") }
                Button { objectName: "exportButton"; Layout.minimumWidth: 0; Layout.maximumWidth: 140; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Export"); enabled: (editor.hasPhoto || library.selectedCount > 0) && !editor.busy && !editor.exporting; onClicked: exportSettings.open() }
                Button { objectName: "openPhotoButton"; Layout.minimumWidth: 0; Layout.maximumWidth: 180; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "+  Open photo"); enabled: !editor.busy; onClicked: fileDialog.open() }
            }
        }
        Rectangle {
            visible: editor.error.length > 0
            Layout.fillWidth: true; Layout.preferredHeight: visible ? errorLayout.implicitHeight + 24 : 0; color: "#42282a"
            RowLayout {
                id: errorLayout; anchors.fill: parent; anchors.margins: 12
                Label { objectName: "main.editorError"; text: qsTranslate("Luma", editor.error); color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 12 }
                Button { objectName: "editor.retrySave"; visible: editor.saving && !editor.busy; text: qsTranslate("Luma", "Retry save"); onClicked: editor.retrySave() }
            }
        }
        RowLayout {
            visible: editor.exporting
            Layout.fillWidth: true; Layout.margins: visible ? 10 : 0
            Label { text: qsTranslate("Luma", "Export"); color: "#dfae77" }
            ProgressBar { value: editor.exportProgress; Layout.fillWidth: true }
            Button { text: qsTranslate("Luma", "Cancel"); onClicked: editor.cancelExport() }
        }
        ColumnLayout {
            visible: exporter.working || exporter.status.length > 0 || exporter.error.length > 0
            Layout.fillWidth: true; Layout.margins: 10
            RowLayout {
                Label { text: qsTranslate("Luma", exporter.status); textFormat: Text.PlainText; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: exporter.completed + " / " + exporter.total }
                Button { objectName: "export.pause"; visible: exporter.working && !exporter.paused; text: exporter.pausePending ? qsTranslate("Luma", "Pause after this file…") : qsTranslate("Luma", "Pause"); enabled: !exporter.pausePending; onClicked: exporter.pause() }
                Button { objectName: "export.resume"; visible: exporter.paused; text: qsTranslate("Luma", "Resume"); onClicked: exporter.resume() }
                Button { objectName: "export.cancel"; visible: exporter.working; text: qsTranslate("Luma", "Cancel"); onClicked: exporter.cancel() }
                Button { text: qsTranslate("Luma", "Settings / errors"); onClicked: exportSettings.open() }
            }
            Label { visible: exporter.working; text: exporter.currentFile; textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true }
            ProgressBar { objectName: "export.progress"; visible: exporter.working; value: exporter.progress; indeterminate: exporter.waitingForSave; Layout.fillWidth: true }
            Label { visible: exporter.error.length > 0; text: qsTranslate("Luma", exporter.error); textFormat: Text.PlainText; color: "#ffc1bc"; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ScrollView {
                objectName: "export.failuresScroll"
                visible: exporter.failures.length > 0
                Layout.fillWidth: true; Layout.preferredHeight: 65
                TextArea { objectName: "export.failures"; readOnly: true; text: exporter.failures.join("\n"); textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; color: "#ffc1bc" }
            }
            Button { objectName: "export.retrySave"; visible: exporter.waitingForSave && editor.saving && !editor.busy; text: qsTranslate("Luma", "Retry save"); onClicked: editor.retrySave() }
        }
        LibraryPane { objectName: "library.pane"; visible: root.libraryMode; Layout.fillWidth: true; Layout.fillHeight: true }
        SplitView {
            visible: !root.libraryMode
            id: panels
            objectName: "develop.panels"
            Layout.fillWidth: true; Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle { implicitWidth: 1; color: "#30343b" }
            Rectangle {
                objectName: "develop.presetsPane"
                SplitView.preferredWidth: 214; SplitView.minimumWidth: 170; SplitView.maximumWidth: 350; color: "#191c22"
                ScrollView {
                    id: presetsScroll
                    objectName: "develop.presetsScroll"
                    Component.onCompleted: ScrollBar.vertical.objectName = "develop.presetsBar"
                    anchors.fill: parent
                    anchors.margins: 20
                    clip: true
                    rightPadding: effectiveScrollBarWidth
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: presetsScroll.availableWidth
                        height: Math.max(implicitHeight, presetsScroll.availableHeight)
                        spacing: 18
                        Label { text: qsTranslate("Luma", "WORKSPACE"); color: "#777f8b"; font.pixelSize: 9; font.letterSpacing: 1 }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 42; radius: 6; color: "#302a25"
                            Label { anchors.centerIn: parent; text: qsTranslate("Luma", "Photo development"); color: "#dfae77"; font.pixelSize: 12 }
                        }
                        Label { text: qsTranslate("Luma", "SOURCE PHOTO"); color: "#777f8b"; font.pixelSize: 9; font.letterSpacing: 1; Layout.topMargin: 10 }
                        Label { objectName: "develop.sourceFilename"; text: editor.hasPhoto ? editor.filename : qsTranslate("Luma", "No photo selected"); color: "#d5d8de"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Label { text: editor.dimensions; color: "#777f8b"; font.pixelSize: 11 }
                        Label { text: qsTranslate("Luma", "Original unchanged"); color: "#858c97"; font.pixelSize: 10; visible: editor.hasPhoto }
                        PresetPanel { Layout.fillWidth: true; Layout.fillHeight: true }
                        Label { text: qsTranslate("Luma", "CATALOG"); color: "#777f8b"; font.pixelSize: 9; font.letterSpacing: 1 }
                        Label { text: editor.catalogPath; color: "#777f8b"; font.pixelSize: 10; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                        Label { text: "LUMA / DEVELOP CORE"; color: "#a28a70"; font.pixelSize: 9; font.letterSpacing: 2 }
                    }
                }
            }
            Rectangle {
                objectName: "develop.canvasPane"
                SplitView.fillWidth: true; SplitView.minimumWidth: 360; color: "#111318"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 18
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: editor.detailDiagnostic.length ? qsTranslate("Luma", editor.detailDiagnostic) : editor.original ? qsTranslate("Luma", "ORIGINAL PHOTO") : editor.presetPreview ? qsTranslate("Luma", "PRESET PREVIEW") : qsTranslate("Luma", "PREVIEW"); color: "#777f8b"; font.pixelSize: 9; font.letterSpacing: 2 }
                        Item { Layout.fillWidth: true }
                        Label { text: qsTranslate("Luma", "FIT"); color: "#777f8b"; font.pixelSize: 10 }
                    }
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Image {
                            id: previewImage
                            objectName: "develop.preview"
                            anchors.fill: parent; source: editor.previewUrl; fillMode: Image.PreserveAspectFit; cache: false; visible: editor.hasPhoto && !editor.busy
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
                        LocalOverlay {
                            x: (previewImage.width-previewImage.paintedWidth)/2
                            y: (previewImage.height-previewImage.paintedHeight)/2
                            width: previewImage.paintedWidth; height: previewImage.paintedHeight
                        }
                        TransformOverlay {
                            x: (previewImage.width-previewImage.paintedWidth)/2
                            y: (previewImage.height-previewImage.paintedHeight)/2
                            width: previewImage.paintedWidth; height: previewImage.paintedHeight
                        }
                        CropOverlay {
                            x: (previewImage.width-previewImage.paintedWidth)/2
                            y: (previewImage.height-previewImage.paintedHeight)/2
                            width: previewImage.paintedWidth; height: previewImage.paintedHeight
                        }
                        ColumnLayout {
                            anchors.centerIn: parent; spacing: 18; visible: !editor.hasPhoto && !editor.busy
                            Label { text: "◉"; font.pixelSize: 72; color: "#dfae77"; Layout.alignment: Qt.AlignHCenter }
                            Label { text: qsTranslate("Luma", "It starts with a photo."); color: "#ece8e2"; font.pixelSize: 25; font.weight: Font.Light; Layout.alignment: Qt.AlignHCenter }
                            Label { text: qsTranslate("Luma", "Open a photo. Find your light."); color: "#858c98"; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                            Button { text: qsTranslate("Luma", "Open photo"); Layout.alignment: Qt.AlignHCenter; onClicked: fileDialog.open() }
                            Label { text: qsTranslate("Luma", "RAW and common images"); color: "#777f8b"; font.pixelSize: 10; Layout.alignment: Qt.AlignHCenter }
                        }
                        ColumnLayout {
                            anchors.centerIn: parent; visible: editor.busy; spacing: 16; width: Math.min(parent.width - 60, 300)
                            BusyIndicator { running: editor.busy; Layout.alignment: Qt.AlignHCenter }
                            Label { text: qsTranslate("Luma", "Decoding image pixels"); color: "#d4d8df"; font.pixelSize: 12; Layout.alignment: Qt.AlignHCenter }
                            ProgressBar { value: editor.progress; Layout.fillWidth: true }
                            Button { objectName: "editor.cancelLoad"; text: qsTranslate("Luma", "Cancel"); onClicked: editor.cancelLoad(); Layout.alignment: Qt.AlignHCenter }
                        }
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter; spacing: 8
                        Button { objectName: "develop.processedButton"; text: qsTranslate("Luma", "Develop"); checkable: true; checked: !editor.original; enabled: editor.hasPhoto && !editor.busy; onClicked: editor.original = false }
                        Button { objectName: "develop.originalButton"; text: qsTranslate("Luma", "Original"); checkable: true; checked: editor.original; enabled: editor.hasPhoto && !editor.busy; onClicked: editor.original = true }
                    }
                    Label { visible:editor.targetedTool!=="none";text:qsTranslate("Luma", "Click a tone or color and drag up / down");color:"#dfae77";font.pixelSize:11;Layout.alignment:Qt.AlignHCenter }
                }
            }
            Rectangle {
                objectName: "develop.adjustmentsPane"
                SplitView.preferredWidth: 290; SplitView.minimumWidth: 250; SplitView.maximumWidth: 400; color: "#191c22"
                ScrollView {
                    id: adjustmentsScroll
                    objectName: "develop.adjustmentsScroll"
                    Component.onCompleted: ScrollBar.vertical.objectName = "develop.adjustmentsBar"
                    anchors.fill: parent
                    anchors.margins: 18
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: adjustmentsScroll.availableWidth
                        spacing: 14
                        LocalPanel { Layout.fillWidth: true }
                        Label { text: qsTranslate("Luma", "Adjustments"); font.pixelSize: 17; font.weight: Font.DemiBold; color: "#e5e7ec" }
                        RowLayout {
                            CheckBox { objectName: "develop.whiteBalance.enabled"; Accessible.name: qsTranslate("Luma", "Enable white balance"); checked: editor.correctionStates.whiteBalance; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("whiteBalance",checked); padding: 0 }
                            Label { text: qsTranslate("Luma", "WHITE BALANCE"); color: "#858c97"; font.pixelSize: 9; font.letterSpacing: 1 }
                        }
                        ComboBox {
                            objectName: "develop.whiteBalance.preset"
                            Layout.fillWidth: true
                            textRole: "label"
                            model: [{ value: "As Shot", label: qsTranslate("Luma", "As Shot") },{ value: "Daylight", label: qsTranslate("Luma", "Daylight") },{ value: "Cloudy", label: qsTranslate("Luma", "Cloudy") },{ value: "Shade", label: qsTranslate("Luma", "Shade") },{ value: "Tungsten", label: qsTranslate("Luma", "Tungsten") },{ value: "Fluorescent", label: qsTranslate("Luma", "Fluorescent") },{ value: "Custom", label: qsTranslate("Luma", "Custom") }]
                            currentIndex: editor.customWhiteBalance ? 6 : 0
                            enabled: editor.hasPhoto && !editor.busy && editor.correctionStates.whiteBalance
                            onActivated: editor.setWhiteBalancePreset(model[currentIndex].value)
                        }
                        DevelopSlider { parameter: "temperature"; label: qsTranslate("Luma", "Temperature"); minimum: 2000; maximum: 25000; step: 50; defaultValue: 6500; suffix: " K"; correction: "whiteBalance"; allowDisable: false; Layout.fillWidth: true }
                        DevelopSlider { parameter: "tint"; label: qsTranslate("Luma", "Tint"); minimum: -150; maximum: 150; correction: "whiteBalance"; allowDisable: false; Layout.fillWidth: true }
                        Label { visible: !editor.customWhiteBalance; text: qsTranslate("Luma", "As Shot keeps the source white balance. Kelvin is available in Custom."); font.pixelSize: 10; color: "#858c97"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#30343b" }
                        Label { text: qsTranslate("Luma", "LIGHT"); color: "#858c97"; font.pixelSize: 9; font.letterSpacing: 2 }
                        DevelopSlider { parameter: "exposure"; label: qsTranslate("Luma", "Exposure"); minimum: -5; maximum: 5; step: 0.01; suffix: " EV"; Layout.fillWidth: true }
                        DevelopSlider { parameter: "contrast"; label: qsTranslate("Luma", "Contrast"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "highlights"; label: qsTranslate("Luma", "Highlights"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "shadows"; label: qsTranslate("Luma", "Shadows"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "whites"; label: qsTranslate("Luma", "Whites"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "blacks"; label: qsTranslate("Luma", "Blacks"); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#30343b" }
                        Label { text: qsTranslate("Luma", "COLOR"); color: "#858c97"; font.pixelSize: 9; font.letterSpacing: 2 }
                        DevelopSlider { parameter: "vibrance"; label: qsTranslate("Luma", "Vibrance"); Layout.fillWidth: true }
                        DevelopSlider { parameter: "saturation"; label: qsTranslate("Luma", "Saturation"); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:"#30343b" }
                        CurvesPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:"#30343b" }
                        HSLPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        Rectangle { Layout.fillWidth:true;implicitHeight:1;color:"#30343b" }
                        CropPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        TransformPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        LensPanel { Layout.fillWidth: true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        DetailPanel { Layout.fillWidth:true; Layout.minimumWidth:0; Layout.maximumWidth:adjustmentsScroll.availableWidth }
                        Button { objectName: "develop.resetButton"; text: qsTranslate("Luma", "Reset all edits"); enabled: editor.hasPhoto && !editor.busy; onClicked: editor.resetEdits(); Layout.fillWidth: true }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: "#30343b" }
                        Label { text: qsTranslate("Luma", "CAPTURE DATA"); color: "#858c97"; font.pixelSize: 9; font.letterSpacing: 1.5 }
                        MetadataRow { label: qsTranslate("Luma", "Camera"); value: editor.camera; Layout.fillWidth: true }
                        MetadataRow { label: qsTranslate("Luma", "Lens"); value: { const language = Qt.uiLanguage; return editor.lens } Layout.fillWidth: true }
                        MetadataRow { label: "ISO"; value: editor.iso; Layout.fillWidth: true }
                        MetadataRow { label: qsTranslate("Luma", "Shutter speed"); value: editor.shutter; Layout.fillWidth: true }
                        MetadataRow { label: qsTranslate("Luma", "Aperture"); value: editor.aperture; Layout.fillWidth: true }
                        MetadataRow { label: qsTranslate("Luma", "Focal length"); value: editor.focalLength; Layout.fillWidth: true }
                        Label { text: editor.saving ? qsTranslate("Luma", "Saving changes…") : qsTranslate("Luma", "Changes are saved automatically"); color: "#858c97"; font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
            }
        }
        ListView {
            id: filmstrip
            objectName: "develop.filmstrip"
            visible: !root.libraryMode && library.count > 0
            Layout.fillWidth: true; Layout.preferredHeight: visible ? 112 : 0
            orientation: ListView.Horizontal; clip: true; spacing: 6; model: library; cacheBuffer: 300
            delegate: PhotoCard { width: 128; height: 104; compact: true }
            ScrollBar.horizontal: ScrollBar { objectName: "develop.filmstripBar" }
        }
        Rectangle {
            objectName: "main.footer"
            Layout.fillWidth: true; implicitHeight: 34; color: "#191c22"
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
                Label { objectName: "applicationVersion"; text: "Luma " + Qt.application.version; font.pixelSize: 10; color: "#a9adb5" }
                Rectangle { width: 5; height: 5; radius: 3; color: editor.busy || editor.saving ? "#dfae77" : "#7bad87" }
                Label { objectName: "main.editorStatus"; text: qsTranslate("Luma", editor.status); font.pixelSize: 10; color: "#858c97"; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: qsTranslate("Luma", "Edits do not change the original image"); color: "#777f8b"; font.pixelSize: 10 }
            }
        }
    }
}
