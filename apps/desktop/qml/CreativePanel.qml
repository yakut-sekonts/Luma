import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: panel
    property int section: sectionPicker.currentValue ?? 256
    property string gradingChannel: wheelPicker.currentValue ?? "shadows"
    spacing: 8
    Button {
        objectName: "develop.autoAdjust"
        text: qsTranslate("Luma", "Auto Adjust"); Layout.fillWidth: true
        enabled: editor.hasPhoto && !editor.busy && !editor.original && !editor.creativeBusy
        onClicked: editor.autoAdjust()
    }
    ComboBox {
        id: sectionPicker; Layout.fillWidth: true; textRole: "label"; valueRole: "value"
        model: [{value:256,label:qsTranslate("Luma","Presence")},{value:512,label:qsTranslate("Luma","Point Color")},
                {value:1024,label:qsTranslate("Luma","Color Grading")},{value:2048,label:qsTranslate("Luma","Effects")}]
    }
    RowLayout {
        visible: panel.section===512; Layout.fillWidth: true
        Button {
            text: qsTranslate("Luma","Eyedropper"); checkable: true; checked: editor.pointColorPicking
            enabled: editor.hasPhoto && !editor.busy && !editor.original && !editor.creativeBusy
            onClicked: editor.pointColorPicking=!editor.pointColorPicking
        }
        CheckBox { text: qsTranslate("Luma","Visualize range"); checked:editor.pointColorRange; onToggled:editor.pointColorRange=checked }
    }
    RowLayout {
        visible: editor.creativeBusy || editor.pointColorPicking; Layout.fillWidth: true
        BusyIndicator { running:editor.creativeBusy; visible:running; implicitWidth:22; implicitHeight:22 }
        Label { text:qsTranslate("Luma","Select a color in the photo"); visible:editor.pointColorPicking; wrapMode:Text.Wrap; Layout.fillWidth:true }
        Button { text:qsTranslate("Luma","Cancel");onClicked:editor.cancelCreative() }
    }
    ComboBox {
        id: wheelPicker; visible:panel.section===1024; Layout.fillWidth:true; textRole:"label";valueRole:"value"
        model:[{value:"shadows",label:qsTranslate("Luma","Shadows")},{value:"midtones",label:qsTranslate("Luma","Midtones")},
               {value:"highlights",label:qsTranslate("Luma","Highlights")},{value:"global",label:qsTranslate("Luma","Global")}]
    }
    ColorWheel { visible:panel.section===1024; channel:panel.gradingChannel; Layout.alignment:Qt.AlignHCenter }
    Repeater {
        model:editor.creativeParameters
        delegate:ColumnLayout {
            required property var modelData
            visible:modelData.group===panel.section &&
                (!modelData.name.startsWith("grading.") || modelData.name==="grading.balance" || modelData.name==="grading.blending" ||
                 modelData.name.startsWith("grading."+panel.gradingChannel+"."))
            Layout.fillWidth:true
            Label {
                visible:modelData.name==="vignette.amount" || modelData.name==="grain.amount"
                text:modelData.name==="grain.amount"?qsTranslate("Luma","Grain"):qsTranslate("Luma","Post Crop Vignette")
                color:"#e5e7ec";font.bold:true
            }
            DevelopSlider {
                parameter:modelData.name;label:qsTranslate("Luma",modelData.label)
                minimum:modelData.minimum;maximum:modelData.maximum;defaultValue:modelData.initial
                correction:panel.section===256?"presence":panel.section===512?"pointColor":panel.section===1024?"grading":modelData.name.startsWith("grain.")?"grain":"vignette"
                Layout.fillWidth:true
            }
        }
    }
}
