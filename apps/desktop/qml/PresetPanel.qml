import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: panel
    objectName: "presetPanel"
    property string selectedId: ""
    property var selected: null
    property string hoverPreviewId: ""
    property var filteredQuickItems: []
    property var libraryModel: null
    readonly property Item editorScrollView: editScroll
    function previewHoveredPreset(presetId) {
        hoverPreviewId = presetId
        presets.preview(presetId)
    }
    function clearHoveredPreview(presetId) {
        if (presetId !== undefined && presetId !== hoverPreviewId) return
        hoverPreviewId = ""
        presets.clearPreview()
    }
    function selection() {
        selected = null
        for (const item of presets.items) {
            if (item.id === selectedId) { selected = item; break }
        }
    }
    function updateQuickItems() {
        if (!quickCategory.currentText.length) { filteredQuickItems = presets.quickItems; return }
        filteredQuickItems = presets.quickItems.filter(item => item.category === quickCategory.currentText)
    }
    function openEditor(create) {
        panel.clearHoveredPreview()
        editDialog.presetId = create ? "" : selectedId
        editDialog.copyMode = false
        nameField.text = create ? "" : selected.name
        folderField.text = create ? "" : selected.folder
        editDialog.groups = create ? 4095 : selected.groups
        capture.checked = create
        editDialog.open()
    }
    onSelectedIdChanged: selection()
    onVisibleChanged: if (!visible) panel.clearHoveredPreview()
    Connections { target: presets; function onItemsChanged() { panel.selection() } }
    Component.onCompleted: updateQuickItems()
    spacing: 8
    TabBar {
        id: presetTabs
        objectName: "presetTabs"
        Layout.fillWidth: true
        onCurrentIndexChanged: panel.clearHoveredPreview()
        TabButton { objectName: "preset.tabs.quick"; text: qsTranslate("Apertone", "Quick"); implicitWidth: 68; font.pixelSize: 11 }
        TabButton { objectName: "preset.tabs.filters"; text: qsTranslate("Apertone", "Filters"); implicitWidth: 68; font.pixelSize: 11 }
        TabButton { objectName: "preset.tabs.presets"; text: qsTranslate("Apertone", "Presets"); implicitWidth: 68; font.pixelSize: 11 }
    }
    ColumnLayout {
        visible: presetTabs.currentIndex === 0
        Layout.fillWidth: true; Layout.fillHeight: true; spacing: 6
        ComboBox {
            id: quickCategory
            objectName: "quick.category"
            Layout.fillWidth: true; model: presets.quickCategories
            onCurrentTextChanged: panel.updateQuickItems()
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 4
            ApertoneButton {
                objectName: "quick.enhance"; text: qsTranslate("Apertone", "Enhance")
                enabled: editor.hasPhoto && !editor.busy && !editor.original && !editor.creativeBusy
                Layout.fillWidth: true
                onClicked: editor.applyEnhance()
                ToolTip.visible: hovered
                ToolTip.text: qsTranslate("Apertone", "Editable local enhancement with explicit processing provenance")
            }
            ApertoneButton {
                objectName: "quick.mono"; text: qsTranslate("Apertone", "B&W")
                enabled: editor.hasPhoto && !editor.busy; Layout.fillWidth: true
                onClicked: presets.applyQuickLook("pure-mono")
            }
            ApertoneButton {
                objectName: "quick.straighten"; text: qsTranslate("Apertone", "Straighten")
                enabled: editor.hasPhoto && !editor.busy && !editor.transformAutoBusy; Layout.fillWidth: true
                onClicked: editor.autoTransform()
            }
        }
        ApertoneButton {
            objectName: "quick.matchLook"
            text: qsTranslate("Apertone", "Match Look…")
            Layout.fillWidth: true
            enabled: editor.hasPhoto && !editor.busy && !editor.original &&
                     panel.libraryModel !== null && panel.libraryModel.selectedCount > 0
            onClicked: matchLookDialog.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTranslate("Apertone", "Transfer the editable style of selected Library photos to the current photo.")
        }
        ListView {
            id: quickList
            objectName: "quick.catalog"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 130
            clip: true; model: panel.filteredQuickItems; spacing: 2
            delegate: ItemDelegate {
                id: quickRow
                required property var modelData
                objectName: "quick.recipe." + modelData.id
                width: quickList.width; height: 38; hoverEnabled: true
                enabled: editor.hasPhoto && !editor.busy && !editor.aiBusy
                contentItem: RowLayout {
                    spacing: 6
                    Label {
                        Layout.fillWidth: true; text: quickRow.modelData.name
                        color: quickRow.palette.buttonText; font.pixelSize: 11
                        elide: Text.ElideRight; textFormat: Text.PlainText
                    }
                    Label {
                        visible: quickRow.modelData.target !== "global"
                        text: quickRow.modelData.target === "subject" ? "S" : quickRow.modelData.target === "sky" ? "☁" : "B"
                        color: Theme.accent; font.pixelSize: 9
                    }
                    Label { text: "+"; color: Theme.muted; font.pixelSize: 13 }
                }
                onClicked: presets.applyQuickLook(modelData.id)
                ToolTip.visible: hovered; ToolTip.delay: 500; ToolTip.text: modelData.description
            }
            ScrollBar.vertical: ScrollBar {}
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTranslate("Apertone", "Layers"); color: Theme.text; font.pixelSize: 11; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Label { text: presets.quickStack.length + "/32"; color: Theme.muted; font.pixelSize: 10 }
            ApertoneButton {
                objectName: "quick.clear"; text: qsTranslate("Apertone", "Clear"); implicitWidth: 52
                enabled: presets.quickStack.length > 0 && !editor.busy
                onClicked: presets.clearQuickLooks()
            }
        }
        ListView {
            id: quickStack
            objectName: "quick.stack"
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight, 190)
            Layout.minimumHeight: presets.quickStack.length ? 52 : 28
            clip: true; model: presets.quickStack; spacing: 3
            delegate: Rectangle {
                id: layerRow
                required property var modelData
                required property int index
                width: quickStack.width; height: layerRow.modelData.provenance.length ? 80 : 66; radius: 4
                color: Theme.panelRaised; border.color: Theme.divider; border.width: 1
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 5; spacing: 2
                    RowLayout {
                        Layout.fillWidth: true; spacing: 3
                        CheckBox {
                            objectName: "quick.enabled." + layerRow.modelData.id
                            checked: layerRow.modelData.enabled; implicitWidth: 22; implicitHeight: 22
                            onClicked: presets.setQuickLookEnabled(layerRow.modelData.id, checked)
                        }
                        Label { Layout.fillWidth: true; text: layerRow.modelData.name; elide: Text.ElideRight; font.pixelSize: 10; color: Theme.text }
                        ToolButton { text: "↑"; enabled: layerRow.index > 0; onClicked: presets.moveQuickLook(layerRow.modelData.id, -1); implicitWidth: 24; implicitHeight: 22 }
                        ToolButton { text: "↓"; enabled: layerRow.index + 1 < quickStack.count; onClicked: presets.moveQuickLook(layerRow.modelData.id, 1); implicitWidth: 24; implicitHeight: 22 }
                        ToolButton { text: "×"; onClicked: presets.removeQuickLook(layerRow.modelData.id); implicitWidth: 24; implicitHeight: 22 }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 5
                        Slider {
                            id: amount
                            objectName: "quick.amount." + layerRow.modelData.id
                            Layout.fillWidth: true; from: 0; to: 200; stepSize: 1; value: layerRow.modelData.amount
                            onPressedChanged: if (!pressed) presets.setQuickLookAmount(layerRow.modelData.id, value)
                        }
                        Label { text: Math.round(amount.value) + "%"; color: Theme.muted; font.pixelSize: 9; Layout.preferredWidth: 34; horizontalAlignment: Text.AlignRight }
                    }
                    Label {
                        visible: layerRow.modelData.provenance.length > 0
                        Layout.fillWidth: true; text: layerRow.modelData.provenance
                        color: Theme.muted; font.pixelSize: 8; elide: Text.ElideMiddle
                        textFormat: Text.PlainText
                    }
                }
            }
            Label { anchors.centerIn: parent; visible: quickStack.count === 0; text: qsTranslate("Apertone", "Add looks above — they stay editable and stack in order."); color: Theme.muted; font.pixelSize: 10; wrapMode: Text.Wrap; width: parent.width }
            ScrollBar.vertical: ScrollBar {}
        }
    }
    Dialog {
        id: matchLookDialog
        objectName: "quick.matchLookDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(400, parent ? parent.width - 32 : 400)
        modal: true
        title: qsTranslate("Apertone", "Match Look")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: matchAmount.value = 100
        onAccepted: if (panel.libraryModel !== null)
                        editor.applyMatchLook(panel.libraryModel.selectedAssetIDs(), matchAmount.value,
                                              !preserveExposure.checked,
                                              !preserveWhiteBalance.checked)
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTranslate("Apertone", "Selected Library photos become references. The current photo is excluded.")
                wrapMode: Text.Wrap
                color: Theme.text
            }
            Label {
                text: qsTranslate("Apertone", "%1 selected")
                          .arg(panel.libraryModel === null ? 0 : panel.libraryModel.selectedCount)
                color: Theme.muted
                font.pixelSize: 10
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTranslate("Apertone", "Amount"); color: Theme.text }
                Slider {
                    id: matchAmount
                    objectName: "quick.matchLookAmount"
                    Layout.fillWidth: true
                    from: 0; to: 200; stepSize: 1; value: 100
                }
                Label {
                    text: Math.round(matchAmount.value) + "%"
                    color: Theme.muted
                    Layout.preferredWidth: 38
                    horizontalAlignment: Text.AlignRight
                }
            }
            CheckBox {
                id: preserveExposure
                objectName: "quick.matchLookPreserveExposure"
                text: qsTranslate("Apertone", "Preserve Exposure")
                checked: true
            }
            CheckBox {
                id: preserveWhiteBalance
                objectName: "quick.matchLookPreserveWhiteBalance"
                text: qsTranslate("Apertone", "Preserve White Balance")
                checked: true
            }
        }
    }
    ListView {
        id: builtinList
        objectName: "builtinFilterList"
        visible: presetTabs.currentIndex === 1
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100
        clip: true; model: presets.builtinItems
        delegate: ItemDelegate {
            id: filterRow
            required property var modelData
            objectName: "filter_" + modelData.id.substring(8)
            width: builtinList.width; height: 30; text: modelData.name; hoverEnabled: true
            enabled: editor.hasPhoto && !editor.busy
            onHoveredChanged: {
                if (hovered && enabled) panel.previewHoveredPreset(modelData.id)
                else if (!hovered) panel.clearHoveredPreview(modelData.id)
            }
            onClicked: presets.apply(modelData.id)
            ToolTip.visible: hovered; ToolTip.delay: 700
            ToolTip.text: qsTranslate("Apertone", "Curves and HSL. Exposure, white balance, Detail and geometry are preserved. One Undo step.")
        }
        ScrollBar.vertical: ScrollBar {}
    }
    RowLayout {
        visible: presetTabs.currentIndex === 2
        Layout.fillWidth: true; spacing: 4
        ApertoneButton { objectName: "presetCreate"; Accessible.name: qsTranslate("Apertone", "Save current settings"); implicitWidth: 36; text: "+"; enabled: editor.hasPhoto && !editor.busy; onClicked: panel.openEditor(true); ToolTip.visible: hovered; ToolTip.text: qsTranslate("Apertone", "Save current settings") }
        ApertoneButton { objectName: "presetImport"; text: qsTranslate("Apertone", "Import"); onClicked: importDialog.open(); Layout.fillWidth: true }
        ApertoneButton { objectName: "preset.actions"; Accessible.name: qsTranslate("Apertone", "Preset actions"); implicitWidth: 36; text: "⋯"; onClicked: actions.open()
            Menu {
                id: actions
                objectName: "preset.actions.menu"
                MenuItem { objectName: "preset.actions.edit"; text: qsTranslate("Apertone", "Edit / rename"); enabled: panel.selected !== null; onTriggered: panel.openEditor(false) }
                MenuItem { objectName: "preset.actions.export"; text: qsTranslate("Apertone", "Export JSON…"); enabled: panel.selected !== null; onTriggered: { exportDialog.presetId = panel.selectedId; exportDialog.open() } }
                MenuItem { objectName: "preset.actions.delete"; text: qsTranslate("Apertone", "Delete…"); enabled: panel.selected !== null; onTriggered: deleteDialog.open() }
                MenuSeparator {}
                MenuItem { objectName: "preset.actions.refresh"; text: qsTranslate("Apertone", "Refresh"); onTriggered: presets.refresh() }
            }
        }
    }
    ListView {
        id: list
        objectName: "presetList"
        visible: presetTabs.currentIndex === 2
        Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 100
        clip: true; model: presets.items
        section.property: "folder"; section.criteria: ViewSection.FullString
        section.delegate: Label {
            required property string section
            objectName: "presetFolderTitle"; textFormat: Text.PlainText
            text: section.length ? section : qsTranslate("Apertone", "No folder")
            color: Theme.muted; font.pixelSize: 10; height: 26; topPadding: 7
        }
        delegate: ItemDelegate {
            id: presetRow
            required property var modelData
            objectName: "preset." + modelData.id
            width: list.width; height: 34; hoverEnabled: true
            contentItem: Label {
                objectName: "presetTitle"
                text: presetRow.modelData.name; textFormat: Text.PlainText
                font: presetRow.font; color: presetRow.palette.buttonText
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            text: modelData.name; highlighted: panel.selectedId === modelData.id
            onHoveredChanged: {
                if (hovered && editor.hasPhoto && !editor.busy) panel.previewHoveredPreset(modelData.id)
                else if (!hovered) panel.clearHoveredPreview(modelData.id)
            }
            onClicked: { panel.selectedId = modelData.id; presets.apply(modelData.id) }
            ToolTip.visible: hovered; ToolTip.delay: 700
            ToolTip.text: qsTranslate("Apertone", "Click to apply. Undo restores all preset settings.")
        }
        ScrollBar.vertical: ScrollBar {}
        Label {
            anchors.centerIn: parent; width: parent.width
            visible: list.count === 0; text: qsTranslate("Apertone", "Save your settings or import a JSON/XMP preset.")
            color: Theme.muted; wrapMode: Text.Wrap; font.pixelSize: 11
        }
    }
    Label { objectName: "preset.error"; text: presets.error; visible: text.length > 0; color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
    Label { objectName: "presetImportNotice"; text: qsTranslate("Apertone", presets.importNotice); textFormat: Text.PlainText; visible: text.length > 0; color: Theme.accent; wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: 11 }
    ApertoneButton { objectName: "presetImportDetails"; visible: presets.importNotice.length > 0; text: qsTranslate("Apertone", "XMP compatibility"); Layout.fillWidth: true; onClicked: compatibilityDialog.open() }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        ApertoneButton { objectName: "preset.copy"; implicitWidth: 80; Layout.minimumWidth: 0; text: qsTranslate("Apertone", "Copy…"); enabled: editor.hasPhoto && !editor.busy; Layout.fillWidth: true
            onClicked: { panel.clearHoveredPreview(); editDialog.copyMode = true; editDialog.groups = 4095; editDialog.open() }
        }
        ApertoneButton { objectName: "preset.paste"; implicitWidth: 80; Layout.minimumWidth: 0; text: qsTranslate("Apertone", "Paste"); enabled: presets.hasCopiedSettings && editor.hasPhoto && !editor.busy; onClicked: presets.pasteSettings(); Layout.fillWidth: true }
    }
    Dialog {
        id: editDialog
        objectName: "presetEditor"
        property string presetId: ""
        property bool copyMode: false
        property int groups: 4095
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430; implicitWidth: 430; modal: true
        height: Math.min(implicitHeight, parent.height - 32)
        title: copyMode ? qsTranslate("Apertone", "Copy settings") : presetId.length ? qsTranslate("Apertone", "Edit preset") : qsTranslate("Apertone", "Save preset")
        contentItem: ScrollView {
            id: editScroll
            objectName: "preset.editorScroll"
            clip: true
            implicitHeight: editFields.implicitHeight
            contentWidth: availableWidth
            rightPadding: effectiveScrollBarWidth
            ColumnLayout {
                id: editFields
                width: editScroll.availableWidth; spacing: 10
                ApertoneTextField { id: nameField; objectName: "presetName"; visible: !editDialog.copyMode; placeholderText: qsTranslate("Apertone", "Name"); maximumLength: 80; Layout.fillWidth: true }
                ApertoneTextField { id: folderField; objectName: "presetFolder"; visible: !editDialog.copyMode; placeholderText: qsTranslate("Apertone", "Folder (optional)"); maximumLength: 80; Layout.fillWidth: true }
                Label { text: qsTranslate("Apertone", "Settings groups"); color: Theme.text }
                Repeater {
                    model: [qsTranslate("Apertone", "Light"), qsTranslate("Apertone", "Color"), qsTranslate("Apertone", "Curve"), "HSL", qsTranslate("Apertone", "Detail"), qsTranslate("Apertone", "Crop"), qsTranslate("Apertone", "Optics"), qsTranslate("Apertone", "Geometry"), qsTranslate("Apertone", "Presence"), qsTranslate("Apertone", "Point Color"), qsTranslate("Apertone", "Color Grading"), qsTranslate("Apertone", "Effects")]
                    CheckBox {
                        required property string modelData
                        required property int index
                        objectName: "presetGroup" + index
                        text: modelData; checked: (editDialog.groups & (1 << index)) !== 0
                        onClicked: editDialog.groups = checked ? editDialog.groups | (1 << index) : editDialog.groups & ~(1 << index)
                    }
                }
                CheckBox {
                    id: capture; objectName: "preset.capture"; visible: !editDialog.copyMode && editDialog.presetId.length > 0
                    text: qsTranslate("Apertone", "Replace settings with current edits")
                    enabled: editor.hasPhoto && !editor.busy
                    Layout.fillWidth: true
                    contentItem: Label {
                        text: capture.text
                        font: capture.font
                        color: capture.palette.windowText
                        leftPadding: capture.indicator.width + capture.spacing
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap
                    }
                }
                Label { objectName: "preset.editorHelp"; visible: !editDialog.copyMode; text: qsTranslate("Apertone", "Renaming or moving preserves settings. Replace with current edits to add new groups."); color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 11 }
                Label { objectName: "preset.editorError"; text: presets.error; visible: text.length > 0; color: Theme.negative; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
        footer: DialogButtonBox {
            ApertoneButton { objectName: "preset.cancel"; text: qsTranslate("Apertone", "Cancel"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: editDialog.close() }
            ApertoneButton {
                objectName: "presetSave"
                text: editDialog.copyMode ? qsTranslate("Apertone", "Copy") : qsTranslate("Apertone", "Save")
                enabled: editDialog.groups !== 0 && (editDialog.copyMode || nameField.text.trim().length > 0)
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    if (editDialog.copyMode) { presets.copySettings(editDialog.groups); editDialog.close() }
                    else if (presets.save(editDialog.presetId, nameField.text, folderField.text, editDialog.groups,
                                         !editDialog.presetId.length || (capture.checked && capture.enabled))) editDialog.close()
                }
            }
        }
    }
    Dialog {
        id: deleteDialog
        objectName: "preset.deleteDialog"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        width: 360; implicitWidth: 360
        title: qsTranslate("Apertone", "Delete preset?")
        property string presetId: ""
        onOpened: {
            presetId = panel.selectedId
            standardButton(Dialog.Ok).objectName = "preset.deleteConfirm"
        }
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { textFormat: Text.PlainText; text: panel.selected ? panel.selected.name : ""; color: Theme.text }
        onAccepted: presets.remove(presetId)
    }
    Dialog {
        id: compatibilityDialog
        objectName: "xmpCompatibilityDialog"
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        width: Math.min(520, parent.width - 32); height: Math.min(460, parent.height - 32)
        title: qsTranslate("Apertone", "XMP compatibility")
        contentItem: ColumnLayout {
            Label { text: qsTranslate("Apertone", presets.importNotice); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { visible: presets.importUnsupported.length > 0; text: qsTranslate("Apertone", "These XMP settings were not imported. Other recognized settings remain available in the preset."); textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            ListView {
                objectName: "xmpUnsupportedList"; model: presets.importUnsupported
                clip: true; Layout.fillWidth: true; Layout.fillHeight: true
                delegate: Label { required property string modelData; text: modelData; textFormat: Text.PlainText; width: ListView.view.width; wrapMode: Text.Wrap; padding: 6 }
                ScrollBar.vertical: ScrollBar {}
            }
        }
        footer: DialogButtonBox {
            ApertoneButton { text: qsTranslate("Apertone", "Close"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole; onClicked: compatibilityDialog.close() }
        }
    }
    FileDialog {
        id: importDialog; objectName: "presetImportDialog"; title: qsTranslate("Apertone", "Import preset")
        nameFilters: [qsTranslate("Apertone", "Preset files (*.json *.xmp)"), "Apertone preset (*.json)", "XMP (*.xmp)"]
        onAccepted: if (presets.importPreset(selectedFile)) { panel.selectedId = presets.importedId; presetTabs.currentIndex = 2 }
    }
    FileDialog {
        id: exportDialog; objectName: "presetExportDialog"; property string presetId: ""
        title: qsTranslate("Apertone", "Export Apertone preset"); fileMode: FileDialog.SaveFile
        nameFilters: ["Apertone preset (*.json)"]; defaultSuffix: "json"
        onAccepted: presets.exportPreset(presetId, selectedFile)
    }
}
