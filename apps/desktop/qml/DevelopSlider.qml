import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control
    Layout.minimumWidth: 0
    required property string parameter
    required property string label
    property real minimum: -100
    property real maximum: 100
    property real step: 1
    property real defaultValue: 0
    property string suffix: ""
    property string correction: parameter
    property bool allowDisable: true
    property bool detailDiagnostic: false
    property bool available: editor.hasPhoto && !editor.busy
    spacing: 2
    RowLayout {
        Layout.minimumWidth: 0
        Layout.fillWidth: true
        CheckBox {
            objectName: "develop." + control.parameter + ".enabled"
            visible: control.allowDisable
            checked: editor.correctionStates[control.correction] ?? true
            enabled: control.available
            Accessible.name: qsTranslate("Luma", "Enable ") + control.label
            onToggled: editor.setCorrectionEnabled(control.correction, checked)
            padding: 0
        }
        Label { text: control.label; color: "#d7dce3"; font.pixelSize: 12; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight }
        Label {
            objectName: "develop." + control.parameter + ".value"
            text: {
                const decimals = control.step < 1 ? 2 : 0
                const formatted = Number(editor.parameters[control.parameter]).toFixed(decimals)
                return (Number(formatted) === 0 ? (0).toFixed(decimals) : formatted) + control.suffix
            }
            color: "#dfae77"; font.pixelSize: 12
        }
    }
    Slider {
        id: slider
        property bool resetRequested: false
        objectName: control.detailDiagnostic ? control.parameter + "Slider" : "develop." + control.parameter + "Slider"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        from: control.minimum; to: control.maximum; stepSize: control.step
        value: editor.parameters[control.parameter]
        enabled: control.available && (editor.correctionStates[control.correction] ?? true)
        Accessible.name: control.label
        onPressedChanged: {
            if (pressed) {
                editor.beginEdit()
                if (control.detailDiagnostic)
                    editor.setDetailControl(control.parameter)
            } else {
                if (resetRequested) {
                    resetRequested = false
                    editor.setParameter(control.parameter, control.defaultValue)
                }
                editor.endEdit()
            }
        }
        onMoved: { if (!resetRequested) editor.setParameter(control.parameter, value) }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: slider.resetRequested = true
        }
    }
}
