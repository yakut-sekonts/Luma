import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "hsl.panel"
    property var bands: ["red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"]
    property var colors: ["#d05e60", "#dc9757", "#d6c86a", "#80b678", "#6cbfbc", "#709acb",
        "#a088ce", "#c480b2"]
    property int selected: 0
    property string band: bands[selected]
    property var parameters: ["hue", "saturation", "luminance"]
    spacing: 8
    RowLayout {
        CheckBox {
            checked: editor.correctionStates.hsl
            enabled: editor.hasPhoto && !editor.busy
            onToggled: editor.setCorrectionEnabled("hsl", checked)
            padding: 0
            Accessible.name: qsTranslate("Luma", "Enable HSL")
        }
        Label {
            text: qsTranslate("Luma", "HSL / COLOR MIXER")
            color: "#858c97"
            font.pixelSize: 9
            font.letterSpacing: 1
            Layout.fillWidth: true
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 3
        Repeater {
            model: 8
            Button {
                id: bandButton
                required property int index
                objectName: "hsl.band." + panel.bands[index]
                implicitWidth: 25
                implicitHeight: 26
                Layout.fillWidth: true
                enabled: editor.hasPhoto && !editor.busy && editor.correctionStates.hsl
                background: Rectangle {
                    color: panel.colors[bandButton.index]
                    radius: width / 2
                    border.width: panel.selected === bandButton.index ? 2 : 0
                    border.color: "#f0ede9"
                    opacity: bandButton.enabled ? 1 : 0.4
                }
                onClicked: panel.selected = index
                Accessible.name: panel.bands[index]
                ToolTip.visible: hovered
                ToolTip.text: panel.bands[index]
            }
        }
    }
    Label {
        text: panel.band.toUpperCase()
        color: panel.colors[panel.selected]
        font.pixelSize: 10
    }
    DevelopSlider {
        parameter: "hsl." + panel.band + ".hue"
        label: qsTranslate("Luma", "Hue")
        correction: "hsl"
        allowDisable: false
        Layout.fillWidth: true
    }
    DevelopSlider {
        parameter: "hsl." + panel.band + ".saturation"
        label: qsTranslate("Luma", "Saturation")
        correction: "hsl"
        allowDisable: false
        Layout.fillWidth: true
    }
    DevelopSlider {
        parameter: "hsl." + panel.band + ".luminance"
        label: qsTranslate("Luma", "Luminance")
        correction: "hsl"
        allowDisable: false
        Layout.fillWidth: true
    }
    RowLayout {
        ComboBox {
            id: targetMode
            objectName: "hsl.targetMode"
            model: [qsTranslate("Luma", "Hue"), qsTranslate("Luma", "Saturation"), qsTranslate("Luma", "Luminance")]
            Layout.fillWidth: true
            enabled: editor.hasPhoto && !editor.busy && editor.correctionStates.hsl
            onActivated: if (editor.targetedTool.startsWith("hsl."))
                             editor.targetedTool = "hsl." + panel.parameters[currentIndex]
            Accessible.name: qsTranslate("Luma", "Targeted HSL parameter")
        }
        Button {
            objectName: "hsl.target"
            text: "◎"
            checkable: true
            checked: editor.targetedTool.startsWith("hsl.")
            enabled: editor.hasPhoto && !editor.busy && !editor.original
                     && editor.correctionStates.hsl
            onClicked: editor.targetedTool = checked ? "hsl."
                                                       + panel.parameters[targetMode.currentIndex] :
                                                       "none"
            Accessible.name: qsTranslate("Luma", "Targeted HSL")
            ToolTip.visible: hovered
            ToolTip.text: qsTranslate("Luma", "Adjust color with a photo gesture")
        }
    }
}
