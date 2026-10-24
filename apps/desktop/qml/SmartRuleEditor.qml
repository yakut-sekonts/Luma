import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: root
    required property var rule
    property int depth: 0
    signal edited(var next)
    function change(key,value) { const next=Object.assign({},rule);next[key]=value;edited(next) }
    function replaceChild(index,value) { const children=rule.children.slice();children[index]=value;change("children",children) }
    function removeChild(index) {const children=rule.children.slice();children.splice(index,1);change("children",children)}
    function addChild(group) {
        const child=group?{kind:"all",children:[{kind:"condition",field:"rating",operation:"ge",value:0}]}:{kind:"condition",field:"rating",operation:"ge",value:0}
        change("children",rule.children.concat([child]))
    }
    RowLayout {
        Layout.fillWidth:true
        ComboBox {
            visible:root.rule.kind!=="condition"
            model:["AND","OR","NOT"]
            currentIndex:root.rule.kind==="all"?0:root.rule.kind==="any"?1:2
            onActivated: {
                const next=Object.assign({},root.rule,{kind:["all","any","not"][currentIndex]})
                if(next.kind==="not")next.children=next.children.slice(0,1)
                root.edited(next)
            }
            Accessible.name:qsTranslate("Luma","Rule group")
        }
        ComboBox {
            visible:root.rule.kind==="condition";Layout.fillWidth:true
            property var keys:["rating","flag","iso","camera","lens","filename","captureDate","label","keyword","width","height"]
            model:[qsTranslate("Luma","Rating"),qsTranslate("Luma","Flag"),"ISO",qsTranslate("Luma","Camera"),qsTranslate("Luma","Lens"),qsTranslate("Luma","Filename"),qsTranslate("Luma","Capture date"),qsTranslate("Luma","Color label"),qsTranslate("Luma","Keyword"),qsTranslate("Luma","Width"),qsTranslate("Luma","Height")]
            currentIndex:Math.max(0,keys.indexOf(root.rule.field))
            onActivated:root.change("field",keys[currentIndex])
            Accessible.name:qsTranslate("Luma","Rule field")
        }
        ComboBox {
            visible:root.rule.kind==="condition"
            property var keys:["eq","ne","ge","gt","le","lt","contains"]
            model:["=","≠","≥",">","≤","<",qsTranslate("Luma","Contains")]
            currentIndex:Math.max(0,keys.indexOf(root.rule.operation))
            onActivated:root.change("operation",keys[currentIndex])
            Accessible.name:qsTranslate("Luma","Rule comparison")
        }
        TextField {
            visible:root.rule.kind==="condition";Layout.fillWidth:true;Layout.minimumWidth:50
            text:String(root.rule.value??"");maximumLength:1024;selectByMouse:true
            onEditingFinished:if(text!==String(root.rule.value))root.change("value",text)
            Accessible.name:qsTranslate("Luma","Rule value")
        }
    }
    Repeater {
        model:root.rule.kind==="condition"?[]:root.rule.children
        delegate:RowLayout {
            required property var modelData
            required property int index
            Layout.fillWidth:true;Layout.leftMargin:12
            Loader {
                Layout.fillWidth:true
                Component.onCompleted:setSource("SmartRuleEditor.qml",{rule:modelData,depth:root.depth+1})
                onLoaded: {item.edited.connect(function(next){root.replaceChild(index,next)})}
            }
            Button {text:"−";enabled:root.rule.children.length>1;onClicked:root.removeChild(index);Accessible.name:qsTranslate("Luma","Remove rule")}
        }
    }
    RowLayout {
        visible:root.rule.kind!=="condition" && root.rule.kind!=="not"
        enabled:root.rule.kind!=="condition" && root.depth<7 && root.rule.children.length<16
        Button {text:qsTranslate("Luma","Add condition");onClicked:root.addChild(false)}
        Button {text:qsTranslate("Luma","Add group");onClicked:root.addChild(true)}
    }
}
