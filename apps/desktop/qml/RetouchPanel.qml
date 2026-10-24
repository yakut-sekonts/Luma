import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Button {
        objectName: "retouch.toggle"
        text: qsTranslate("Luma", "Retouch")
        checkable: true
        checked: editor.retouchMode !== "off"
        enabled: editor.hasPhoto && !editor.busy && !editor.editingLocked && !editor.original
        onClicked: editor.retouchMode = checked ? "heal" : "off"
        Layout.fillWidth: true
    }
    ColumnLayout {
        visible: editor.retouchMode !== "off"
        enabled: editor.hasPhoto && !editor.busy && !editor.editingLocked && !editor.original
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [{key: "clone", title: qsTranslate("Luma", "Clone")},
                        {key: "heal", title: qsTranslate("Luma", "Heal")},
                        {key: "remove", title: qsTranslate("Luma", "Remove")}]
                Button {
                    required property var modelData
                    objectName: "retouch.mode." + modelData.key
                    text: modelData.title
                    highlighted: editor.retouchMode === modelData.key
                    onClicked: editor.retouchMode = modelData.key
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
            }
        }
        Label {
            text: editor.retouchMode === "remove"
                ? qsTranslate("Luma", "Paint over a small object to find a replacement area.")
                : qsTranslate("Luma", "Option-click a source area, then paint over the target.")
            wrapMode: Text.Wrap; font.pixelSize: 11; color: "#858c97"
            Layout.fillWidth: true
        }
        Repeater {
            model: [{key: "radius", title: qsTranslate("Luma", "Size"), min: .1, max: 25},
                    {key: "feather", title: qsTranslate("Luma", "Feather"), min: 0, max: 100},
                    {key: "opacity", title: qsTranslate("Luma", "Opacity"), min: 0, max: 100}]
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Label { text: modelData.title + " · " + Number(editor.retouchState[modelData.key]).toFixed(1) }
                Slider {
                    Layout.fillWidth: true
                    from: modelData.min; to: modelData.max
                    value: editor.retouchState[modelData.key]
                    enabled: !editor.retouchBusy
                    Accessible.name: modelData.title
                    onPressedChanged: if (pressed) editor.beginEdit(); else editor.endEdit()
                    onMoved: editor.setRetouchControl(modelData.key, value)
                }
            }
        }
        ComboBox {
            id: strokes
            Layout.fillWidth: true
            model: editor.retouchItems
            textRole: "number"
            Accessible.name: qsTranslate("Luma", "Retouch stroke")
            currentIndex: {
                for (let i = 0; i < model.length; ++i)
                    if (model[i].id === editor.retouchState.selected) return i
                return -1
            }
            onActivated: editor.selectRetouch(model[currentIndex].id)
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: qsTranslate("Luma", "New stroke"); onClicked: editor.selectRetouch(""); Layout.fillWidth: true }
            Button {
                text: qsTranslate("Luma", "Delete")
                enabled: editor.retouchState.selected.length > 0
                onClicked: editor.removeRetouch()
                Layout.fillWidth: true
            }
        }
        CheckBox {
            visible: editor.retouchState.selected.length > 0
            text: qsTranslate("Luma", "Enabled")
            checked: editor.retouchState.enabled
            onToggled: editor.setRetouchEnabled(checked)
        }
        Label {
            visible: editor.retouchState.selected.length > 0
            text: qsTranslate("Luma", "Option-click to reposition the selected source.")
            wrapMode: Text.Wrap; font.pixelSize: 11; color: "#858c97"
            Layout.fillWidth: true
        }
        ProgressBar { objectName: "retouch.progress"; visible: editor.retouchBusy; indeterminate: true; Layout.fillWidth: true }
        Button {
            objectName: "retouch.cancel"
            visible: editor.retouchBusy || editor.retouchState.drawing
            text: qsTranslate("Luma", "Cancel")
            onClicked: editor.cancelRetouch()
            Layout.fillWidth: true
        }
    }
}
