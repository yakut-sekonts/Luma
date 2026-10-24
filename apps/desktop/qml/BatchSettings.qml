import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    id: row
    columns: width < 900 ? 3 : 6
    columnSpacing: 6
    rowSpacing: 6
    objectName: "batchSettings"
    Layout.minimumWidth: 0
    Layout.fillWidth: true
    property bool ready: library.selectedCount > 0 && !library.working && !library.selecting && !editor.busy && !editor.exporting
    property var availablePresets: presets.builtinItems.concat(presets.items)
    ComboBox {
        Layout.minimumWidth: 0;
        id: presetChoice; objectName: "batchPresetChoice"
        model: row.availablePresets; textRole: "name"; Layout.preferredWidth: 190; Layout.maximumWidth: 230
        enabled: row.ready && row.availablePresets.length > 0
        delegate: ItemDelegate {
            required property var modelData
            width: presetChoice.width
            contentItem: Label { text: modelData.folder ? modelData.folder + " / " + modelData.name : modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight }
        }
        contentItem: Label { text: presetChoice.currentText; textFormat: Text.PlainText; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight; leftPadding: 8 }
    }
    Button { objectName: "batchApplyPreset"; Layout.minimumWidth: 0; Layout.maximumWidth: 110; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Apply"); enabled: row.ready && presetChoice.currentIndex >= 0; onClicked: batch.applyPreset(row.availablePresets[presetChoice.currentIndex].id) }
    Button { objectName: "batchPaste"; Layout.minimumWidth: 0; Layout.maximumWidth: 110; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Paste"); enabled: row.ready && presets.hasCopiedSettings; onClicked: batch.paste() }
    Button { objectName: "batchSync"; Layout.minimumWidth: 0; Layout.maximumWidth: 200; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Sync Settings…"); enabled: row.ready && editor.hasPhoto; onClicked: syncDialog.open() }
    Button { objectName: "batchUndo"; Layout.minimumWidth: 0; Layout.maximumWidth: 160; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Undo batch"); enabled: batch.canUndo && !library.working && !editor.busy && !editor.exporting; onClicked: batch.undo() }
    Button { objectName: "batchRedo"; Layout.minimumWidth: 0; Layout.maximumWidth: 160; ToolTip.visible: hovered; ToolTip.text: text; text: qsTranslate("Luma", "Redo batch"); enabled: batch.canRedo && !library.working && !editor.busy && !editor.exporting; onClicked: batch.redo() }
    Dialog {
        id: syncDialog; objectName: "batchSyncDialog"
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 430; implicitWidth: 430; modal: true
        title: qsTranslate("Luma", "Sync Settings")
        property int groups: 4095
        ColumnLayout {
            width: parent.width
            Label { text: qsTranslate("Luma", "Source: ") + editor.filename; textFormat: Text.PlainText; wrapMode: Text.Wrap; Layout.fillWidth: true }
            Label { text: qsTranslate("Luma", "Selected photos: ") + library.selectedCount + qsTranslate("Luma", ". Only the selected groups will change."); wrapMode: Text.Wrap; Layout.fillWidth: true }
            Repeater {
                model: [qsTranslate("Luma", "Light"), qsTranslate("Luma", "Color"), qsTranslate("Luma", "Curve"), "HSL", qsTranslate("Luma", "Detail"), qsTranslate("Luma", "Crop"), qsTranslate("Luma", "Optics"), qsTranslate("Luma", "Geometry"),qsTranslate("Luma","Presence"),qsTranslate("Luma","Point Color"),qsTranslate("Luma","Color Grading"),qsTranslate("Luma","Effects")]
                CheckBox {
                    required property string modelData
                    required property int index
                    text: modelData; checked: (syncDialog.groups & (1 << index)) !== 0
                    onClicked: syncDialog.groups = checked ? syncDialog.groups | (1 << index) : syncDialog.groups & ~(1 << index)
                }
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTranslate("Luma", "Cancel"); onClicked: syncDialog.close(); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Button { objectName: "batchSyncConfirm"; text: qsTranslate("Luma", "Apply to selection"); enabled: syncDialog.groups !== 0 && row.ready; DialogButtonBox.buttonRole: DialogButtonBox.ActionRole; onClicked: { batch.sync(syncDialog.groups); syncDialog.close() } }
        }
    }
}
