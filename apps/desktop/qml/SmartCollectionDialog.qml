import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Dialog {
    id: dialog
    parent:Overlay.overlay;anchors.centerIn:parent;modal:true
    width:Math.min(parent.width-40,760);height:Math.min(parent.height-40,640)
    title:qsTranslate("Luma","Smart Collection")
    property string collectionID:""
    property string revision:"0"
    property var rule:({kind:"all",children:[{kind:"condition",field:"rating",operation:"ge",value:4}]})
    function create() {collectionID="";revision="0";name.text="";rule={kind:"all",children:[{kind:"condition",field:"rating",operation:"ge",value:4}]};open()}
    Connections {
        target:library
        function onSmartCollectionReady(id,title,version,tree) {dialog.collectionID=id;name.text=title;dialog.revision=version;dialog.rule=tree;dialog.open()}
    }
    ColumnLayout {
        anchors.fill:parent
        TextField {id:name;Layout.fillWidth:true;maximumLength:128;placeholderText:qsTranslate("Luma","Collection name");selectByMouse:true}
        Label {text:qsTranslate("Luma","Membership updates automatically. Date values use YYYY-MM-DD.");wrapMode:Text.Wrap;Layout.fillWidth:true}
        ScrollView {
            id:rulesScroll
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
            SmartRuleEditor {width:rulesScroll.availableWidth;rule:dialog.rule;onEdited:function(next){dialog.rule=next}}
        }
        Label {visible:library.error.length>0;text:qsTranslate("Luma",library.error);wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#ffc1bc"}
    }
    footer:DialogButtonBox {
        Button {text:qsTranslate("Luma","Cancel");onClicked:dialog.close()}
        Button {text:qsTranslate("Luma","Save");enabled:name.text.trim().length>0&&!library.working;onClicked:{dialog.saving=true;library.saveSmartCollection(dialog.collectionID,name.text,dialog.revision,dialog.rule)}}
    }
    Connections {target:library;function onStateChanged(){if(dialog.visible&&!library.working&&dialog.saving){dialog.saving=false;if(library.error.length===0)dialog.close()}}}
    property bool saving:false
    onOpened:saving=false
}
