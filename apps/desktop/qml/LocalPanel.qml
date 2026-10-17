import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "localPanel"
    Layout.preferredWidth: 250
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.maximumWidth: parent.width
    property bool selected: !!editor.localState.id
    readonly property var kinds: ["linear", "radial", "brush", "luminance", "color"]
    readonly property var operations: ["add", "subtract", "intersect"]
    function kindName(kind) {
        switch(kind) {
        case "linear": return qsTranslate("Luma", "Linear gradient")
        case "radial": return qsTranslate("Luma", "Radial gradient")
        case "brush": return qsTranslate("Luma", "Brush")
        case "luminance": return qsTranslate("Luma", "Luminance range")
        case "color": return qsTranslate("Luma", "Color range")
        }
        return ""
    }
    function operationName(operation) {
        return operation === 0 ? qsTranslate("Luma", "Add") : operation === 1 ? qsTranslate("Luma", "Subtract") : qsTranslate("Luma", "Intersect")
    }
    readonly property var kindNames: kinds.map(kind => kindName(kind))
    readonly property var operationNames: [qsTranslate("Luma", "Add"), qsTranslate("Luma", "Subtract"), qsTranslate("Luma", "Intersect")]
    Button {
        id: expanded
        objectName: "localPanelToggle"
        text: qsTranslate("Luma", "Local adjustments")
        checkable: true
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
        onToggled: if (!checked) editor.localMode = false
    }
    ColumnLayout {
        visible: expanded.checked
        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
        enabled: editor.hasPhoto && !editor.busy && !editor.editingLocked && !editor.original
        ComboBox {
            id: newKind
            objectName: "localNewKind"
            model: panel.kindNames
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
            Accessible.name: qsTranslate("Luma", "Mask type")
        }
        Button {
            objectName: "localNewMask"
            text: qsTranslate("Luma", "New mask")
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
            onClicked: editor.addLocalMask(panel.kinds[newKind.currentIndex])
        }
        ComboBox {
            objectName: "localMaskChoice"
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
            model: editor.localMasks
            textRole: "name"
            currentIndex: {for(let i=0;i<model.length;++i) if(model[i].id===editor.localState.id)return i;return -1}
            onActivated: {editor.selectLocalMask(model[currentIndex].id);editor.localMode=true}
        }
        Label {
            visible: !panel.selected
            text: qsTranslate("Luma", "Add a mask to adjust part of the photo.")
            wrapMode: Text.Wrap; color: "#858c97"; font.pixelSize: 11
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
        }
        ColumnLayout {
            visible: panel.selected
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
            TextField {
                objectName: "localMaskName"
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
                maximumLength: 80; text: editor.localState.name ?? ""
                onEditingFinished: if(panel.selected)editor.renameLocalMask(text)
                Accessible.name: qsTranslate("Luma", "Mask name")
            }
            LocalCheckBox {objectName:"localMaskEnabled";text: qsTranslate("Luma", "Enabled");checked: editor.localState.enabled ?? false;onToggled: editor.setLocalEnabled(checked)}
            LocalCheckBox {objectName:"localMaskInvert";text: qsTranslate("Luma", "Invert mask");checked: editor.localState.inverted ?? false;onToggled: editor.setLocalInverted(checked)}
            RowLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
                Button {objectName:"localMaskUp";text: "↑";implicitWidth:36;Layout.maximumWidth:36;Accessible.name:qsTranslate("Luma", "Move mask up");onClicked:editor.moveLocalMask(-1)}
                Button {objectName:"localMaskDown";text: "↓";implicitWidth:36;Layout.maximumWidth:36;Accessible.name:qsTranslate("Luma", "Move mask down");onClicked:editor.moveLocalMask(1)}
                Button {objectName:"localMaskDelete";text:qsTranslate("Luma", "Delete mask");Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:editor.removeLocalMask()}
            }
            ComboBox {
                objectName: "localComponentChoice"
                Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.maximumWidth: panel.width
                model: editor.localComponents.map(component => ({id:component.id,label:(component.operation<0?qsTranslate("Luma", "Base selection"):panel.operationName(component.operation))+" · "+panel.kindName(component.shape)}))
                textRole: "label"
                currentIndex: (editor.localState.componentIndex ?? -1)+1
                onActivated: {editor.selectLocalComponent(model[currentIndex].id);editor.localMode=true}
                Accessible.name: qsTranslate("Luma", "Selection component")
            }
            ColumnLayout {
                visible: (editor.localState.componentIndex ?? -1)>=0
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                ComboBox {
                    objectName:"localComponentOperation"
                    Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                    model:panel.operationNames;currentIndex:editor.localState.componentOperation ?? 0
                    onActivated:editor.setLocalComponentOperation(panel.operations[currentIndex])
                    Accessible.name:qsTranslate("Luma", "Component operation")
                }
                LocalCheckBox {objectName:"localComponentEnabled";text:qsTranslate("Luma", "Component enabled");checked:editor.localState.componentEnabled ?? true;onToggled:editor.setLocalComponentEnabled(checked)}
                LocalCheckBox {objectName:"localComponentInvert";text:qsTranslate("Luma", "Invert component");checked:editor.localState.componentInverted ?? false;onToggled:editor.setLocalComponentInverted(checked)}
                RowLayout {
                    Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                    Button {objectName:"localComponentUp";text:"↑";implicitWidth:36;Layout.maximumWidth:36;Accessible.name:qsTranslate("Luma", "Move component up");onClicked:editor.moveLocalComponent(-1)}
                    Button {objectName:"localComponentDown";text:"↓";implicitWidth:36;Layout.maximumWidth:36;Accessible.name:qsTranslate("Luma", "Move component down");onClicked:editor.moveLocalComponent(1)}
                    Button {objectName:"localRemoveComponent";text:qsTranslate("Luma", "Remove component");Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:editor.removeLocalComponent()}
                }
            }
            Button {
                id: combine
                objectName: "localCombineToggle"
                text:qsTranslate("Luma", "Combine selections")
                checkable:true
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
            }
            ColumnLayout {
                visible:combine.checked
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                ComboBox {id:componentKind;objectName:"localComponentKind";model:panel.kindNames;currentIndex:2;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width;Accessible.name:qsTranslate("Luma", "Component type")}
                ComboBox {id:componentOperation;objectName:"localNewOperation";model:panel.operationNames;Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width;Accessible.name:qsTranslate("Luma", "Component operation")}
                Button {objectName:"localAddComponent";text:qsTranslate("Luma", "Add component");Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width;onClicked:editor.addLocalComponent(panel.kinds[componentKind.currentIndex],panel.operations[componentOperation.currentIndex])}
            }
            Button {
                objectName:"localEditOnPhoto"
                text:qsTranslate("Luma", "Edit mask on photo")
                checkable:true;checked:editor.localMode
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                onClicked:editor.localMode=!editor.localMode
            }
            ColumnLayout {
                visible:editor.localState.shape==="brush"
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                Label {Layout.fillWidth:true;Layout.minimumWidth:0;wrapMode:Text.Wrap;text:qsTranslate("Luma", "Brush size")+" · "+Number(editor.localBrushSize).toFixed(1)+"%";font.pixelSize:11}
                Slider {objectName:"localBrushSize";from:.1;to:100;value:editor.localBrushSize;onMoved:editor.localBrushSize=value;Layout.fillWidth:true;Accessible.name:qsTranslate("Luma", "Brush size")}
                Label {Layout.fillWidth:true;Layout.minimumWidth:0;wrapMode:Text.Wrap;text:qsTranslate("Luma", "Brush opacity")+" · "+Number(editor.localBrushOpacity).toFixed(0)+"%";font.pixelSize:11}
                Slider {objectName:"localBrushOpacity";from:0;to:100;value:editor.localBrushOpacity;onMoved:editor.localBrushOpacity=value;Layout.fillWidth:true;Accessible.name:qsTranslate("Luma", "Brush opacity")}
                LocalCheckBox {objectName:"localBrushErase";text:qsTranslate("Luma", "Erase");checked:editor.localBrushErase;onToggled:editor.localBrushErase=checked}
                Button {objectName:"localClearBrush";text:qsTranslate("Luma", "Clear brush strokes");enabled:(editor.localState.strokeCount ?? 0)>0;Layout.fillWidth:true;Layout.minimumWidth:0;onClicked:editor.clearLocalBrush()}
            }
            ColumnLayout {
                visible:editor.localState.shape==="luminance"||editor.localState.shape==="color"
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                Label {
                    text:editor.localSampling?qsTranslate("Luma", "Sampling…"):qsTranslate("Luma", "Click the photo to sample the range.")
                    wrapMode:Text.Wrap;font.pixelSize:11;color:"#dfae77"
                    Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                }
                Rectangle {visible:editor.localState.shape==="color";color:editor.localState.referenceColor ?? "#808080";implicitHeight:22;Layout.fillWidth:true;Accessible.name:qsTranslate("Luma", "Sampled color")}
                LocalSlider {visible:editor.localState.shape==="luminance";parameter:"rangeLow";label:qsTranslate("Luma", "Luminance minimum");minimum:0;maximum:editor.localState.rangeHigh ?? 100;step:.1}
                LocalSlider {visible:editor.localState.shape==="luminance";parameter:"rangeHigh";label:qsTranslate("Luma", "Luminance maximum");minimum:editor.localState.rangeLow ?? 0;maximum:100;step:.1}
                LocalSlider {visible:editor.localState.shape==="color";parameter:"colorTolerance";label:qsTranslate("Luma", "Color tolerance");minimum:0;maximum:100;step:.1}
            }
            LocalSlider {parameter:"feather";label:qsTranslate("Luma", "Feather");minimum:0;maximum:100}
            LocalSlider {visible:editor.localState.shape==="radial";parameter:"angle";label:qsTranslate("Luma", "Rotation");minimum:-180;maximum:180}
            LocalSlider {parameter:"opacity";label:qsTranslate("Luma", "Mask opacity");minimum:0;maximum:100}
            LocalCheckBox {objectName:"localOverlayToggle";text:qsTranslate("Luma", "Show mask overlay");checked:editor.localOverlay;onToggled:editor.localOverlay=checked}
            RowLayout {
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
                Label {text:qsTranslate("Luma", "Overlay color");Layout.fillWidth:true;Layout.minimumWidth:0;font.pixelSize:11}
                Repeater {
                    model:["#ff2666","#00cbea","#67e26a"]
                    Button {
                        required property string modelData
                        objectName:"localOverlayColor."+modelData
                        implicitWidth:28;implicitHeight:28;Layout.preferredWidth:28;Layout.maximumWidth:28
                        Accessible.name:qsTranslate("Luma", "Overlay color")+" "+modelData
                        onClicked:editor.localOverlayColor=modelData
                        background:Rectangle {color:parent.modelData;radius:4;border.width:editor.localOverlayColor===parent.modelData?3:0;border.color:"white"}
                    }
                }
            }
            Label {text:qsTranslate("Luma", "Overlay opacity");font.pixelSize:11}
            Slider {objectName:"localOverlayOpacity";from:0;to:1;value:editor.localOverlayOpacity;onMoved:editor.localOverlayOpacity=value;Layout.fillWidth:true;Layout.minimumWidth:0;Accessible.name:qsTranslate("Luma", "Overlay opacity")}
            LocalSlider {parameter:"exposure";label:qsTranslate("Luma", "Exposure");minimum:-5;maximum:5;step:.01}
            LocalSlider {parameter:"contrast";label:qsTranslate("Luma", "Contrast")}
            LocalSlider {parameter:"highlights";label:qsTranslate("Luma", "Highlights")}
            LocalSlider {parameter:"shadows";label:qsTranslate("Luma", "Shadows")}
            LocalSlider {parameter:"whites";label:qsTranslate("Luma", "Whites")}
            LocalSlider {parameter:"blacks";label:qsTranslate("Luma", "Blacks")}
            LocalSlider {parameter:"vibrance";label:qsTranslate("Luma", "Vibrance")}
            LocalSlider {parameter:"saturation";label:qsTranslate("Luma", "Saturation")}
            Label {
                visible:editor.localMode
                text:editor.localState.shape==="brush"?qsTranslate("Luma", "Paint on the photo; each stroke is one Undo."):qsTranslate("Luma", "Drag empty photo to redraw; drag handles to move or resize.")
                wrapMode:Text.Wrap;font.pixelSize:11;color:"#dfae77"
                Layout.fillWidth:true;Layout.minimumWidth:0;Layout.maximumWidth:panel.width
            }
        }
    }
}
