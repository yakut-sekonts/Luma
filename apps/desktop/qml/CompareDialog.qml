import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id:dialog
    parent:Overlay.overlay;anchors.centerIn:parent;modal:true
    width:parent.width-32;height:parent.height-32
    title:qsTranslate("Luma","Compare Select / Candidate")
    property real centerX:0.5
    property real centerY:0.5
    property bool swapped:false
    onOpened:{centerX=0.5;centerY=0.5;swapped=false;zoomControl.value=1}
    onClosed:editor.closeCompare()
    ColumnLayout {
        anchors.fill:parent
        RowLayout {
            Button {text:qsTranslate("Luma","Fit");onClicked:{zoomControl.value=1;dialog.centerX=0.5;dialog.centerY=0.5;if(editor.comparison.fullResolution)editor.prepareCompare(editor.comparison.ids,false)}}
            Button {text:"100%";Accessible.name:qsTranslate("Luma","Zoom")+" 100%";enabled:!editor.comparison.busy;onClicked:{zoomControl.value=1;editor.prepareCompare(editor.comparison.ids,true)}}
            Label {text:qsTranslate("Luma","Linked zoom and pan")}
            Slider {id:zoomControl;from:1;to:4;value:1;Layout.fillWidth:true;enabled:editor.comparison.ready;Accessible.name:qsTranslate("Luma","Zoom")}
            Button {text:qsTranslate("Luma","Swap");enabled:editor.comparison.ready;onClicked:dialog.swapped=!dialog.swapped}
            Button {text:qsTranslate("Luma","Close");onClicked:dialog.close()}
        }
        BusyIndicator {running:editor.comparison.busy;visible:running;Layout.alignment:Qt.AlignHCenter}
        Label {visible:editor.comparison.error.length>0;text:qsTranslate("Luma",editor.comparison.error);wrapMode:Text.Wrap;Layout.fillWidth:true}
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;visible:editor.comparison.ready
            Repeater {
                model:2
                delegate:ComparePane {
                    required property int index
                    property int side:dialog.swapped?1-index:index
                    Layout.fillWidth:true;Layout.fillHeight:true;Layout.preferredWidth:1
                    source:editor.comparison.ready?(side===0?editor.comparison.left:editor.comparison.right):""
                    title:(index===0?qsTranslate("Luma","Select"):qsTranslate("Luma","Candidate"))+" · "+(editor.comparison.names[side]??"")
                    zoom:zoomControl.value;centerX:dialog.centerX;centerY:dialog.centerY;pixelScale:editor.comparison.fullResolution
                    onPanned:function(x,y){dialog.centerX=x;dialog.centerY=y}
                }
            }
        }
    }
}
