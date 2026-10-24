import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
Dialog {
    id:dialog
    parent:Overlay.overlay;anchors.centerIn:parent;modal:true
    objectName:"library.importDialog"
    title:qsTranslate("Luma","Import photos")
    width:Math.min(parent.width-40,680);height:Math.min(parent.height-40,800)
    property var sources:[]
    property url destination:""
    property url backup:""
    property string selectedMetadataPreset:""
    function metadataValues() {return {title:titleField.text,caption:caption.text,creator:creator.text,copyright:copyright.text,location:location.text,keywords:keywords.text}}
    function useMetadata(values) {titleField.text=values.title??"";caption.text=values.caption??"";creator.text=values.creator??"";copyright.text=values.copyright??"";location.text=values.location??"";keywords.text=values.keywords??""}
    FileDialog {id:files;title:qsTranslate("Luma","Import photos");fileMode:FileDialog.OpenFiles;nameFilters:[qsTranslate("Luma","Photos (*.nef *.NEF *.dng *.DNG *.cr2 *.CR2 *.cr3 *.CR3 *.arw *.ARW *.rw2 *.RW2 *.orf *.ORF *.raf *.RAF *.jpg *.JPG *.jpeg *.JPEG *.png *.PNG *.tif *.TIF *.tiff *.TIFF *.heic *.HEIC *.heif *.HEIF *.webp *.WEBP *.bmp *.BMP *.gif *.GIF *.avif *.AVIF)"),qsTranslate("Luma","All files (*)")];onAccepted:dialog.sources=selectedFiles}
    FolderDialog {id:folder;title:qsTranslate("Luma","Photo folder");onAccepted:dialog.sources=[selectedFolder]}
    FolderDialog {id:destinationPicker;title:qsTranslate("Luma","Choose destination folder");onAccepted:dialog.destination=selectedFolder}
    FolderDialog {id:backupPicker;title:qsTranslate("Luma","Second backup folder");onAccepted:dialog.backup=selectedFolder}
    ScrollView {
        id:scroll;anchors.fill:parent;clip:true;contentWidth:availableWidth
        ColumnLayout {
            width:scroll.availableWidth;spacing:10
            RowLayout {
                Button {text:qsTranslate("Luma","Choose files");onClicked:files.open()}
                Button {text:qsTranslate("Luma","Choose folder");onClicked:folder.open()}
            }
            Label {objectName:"library.importSources";text:dialog.sources.length?qsTranslate("Luma","Sources: ")+dialog.sources.length+" · "+dialog.sources[0]:qsTranslate("Luma","No files or folder selected");wrapMode:Text.WrapAnywhere;Layout.fillWidth:true}
            ComboBox {
                id:mode;objectName:"library.importMode";Layout.fillWidth:true
                model:[qsTranslate("Luma","Add · keep originals in place"),qsTranslate("Luma","Copy · copy into the selected folder"),qsTranslate("Luma","Move · transfer originals after verification")]
            }
            RowLayout {
                visible:mode.currentIndex!==0;Layout.fillWidth:true
                Button {text:qsTranslate("Luma","Choose destination folder");onClicked:destinationPicker.open()}
                Label {text:dialog.destination.toString()||qsTranslate("Luma","No folder selected");elide:Text.ElideMiddle;Layout.fillWidth:true}
            }
            CheckBox {id:dates;visible:mode.currentIndex!==0;text:qsTranslate("Luma","Date folders · YYYY/MM/DD (UTC)")}
            TextField {id:naming;visible:mode.currentIndex!==0;text:"{name}";maximumLength:230;Layout.fillWidth:true;Accessible.name:qsTranslate("Luma","Filename template")}
            Label {visible:mode.currentIndex!==0;text:qsTranslate("Luma","Filename tokens: {name}, {date}, {seq}. The original extension is kept. Missing capture dates use the import date.");wrapMode:Text.Wrap;Layout.fillWidth:true}
            CheckBox {id:recursive;objectName:"library.importRecursive";text:qsTranslate("Luma","Include subfolders");checked:true}
            CheckBox {id:duplicates;objectName:"library.importDuplicates";text:qsTranslate("Luma","Skip duplicates by SHA-256 and size");checked:true}
            RowLayout {
                Layout.fillWidth:true
                Button {text:qsTranslate("Luma","Second backup folder");onClicked:backupPicker.open()}
                Label {text:dialog.backup.toString()||qsTranslate("Luma","None");elide:Text.ElideMiddle;Layout.fillWidth:true}
                Button {text:qsTranslate("Luma","Clear");enabled:dialog.backup.toString().length>0;onClicked:dialog.backup=""}
            }
            Label {text:qsTranslate("Luma","Develop preset")}
            ComboBox {
                id:develop;Layout.fillWidth:true;textRole:"name";valueRole:"id"
                model:[{id:"",name:qsTranslate("Luma","None")}].concat(presets.builtinItems).concat(presets.items)
            }
            Label {text:qsTranslate("Luma","Metadata preset")}
            RowLayout {
                Layout.fillWidth:true
                ComboBox {
                    id:metadataPreset;Layout.fillWidth:true;textRole:"name"
                    model:[{name:qsTranslate("Luma","None"),values:{}}].concat(library.importMetadataPresets)
                    onActivated:{dialog.selectedMetadataPreset=currentIndex>0?model[currentIndex].name:"";dialog.useMetadata(model[currentIndex].values);presetName.text=dialog.selectedMetadataPreset}
                }
                Button {text:qsTranslate("Luma","Delete");enabled:dialog.selectedMetadataPreset.length>0;onClicked:if(library.removeImportMetadataPreset(dialog.selectedMetadataPreset)){dialog.selectedMetadataPreset="";metadataPreset.currentIndex=0}}
            }
            TextField {id:titleField;placeholderText:qsTranslate("Luma","Title");maximumLength:1024;Layout.fillWidth:true}
            TextField {id:caption;placeholderText:qsTranslate("Luma","Caption");maximumLength:16384;Layout.fillWidth:true}
            TextField {id:creator;placeholderText:qsTranslate("Luma","Creator");maximumLength:1024;Layout.fillWidth:true}
            TextField {id:copyright;placeholderText:qsTranslate("Luma","Copyright");maximumLength:2048;Layout.fillWidth:true}
            TextField {id:location;placeholderText:qsTranslate("Luma","Location");maximumLength:2048;Layout.fillWidth:true}
            TextField {id:keywords;placeholderText:qsTranslate("Luma","Keywords, separated by commas");maximumLength:32768;Layout.fillWidth:true}
            Label {text:qsTranslate("Luma","Empty metadata fields preserve existing values. Keywords are added without duplicates.");wrapMode:Text.Wrap;Layout.fillWidth:true}
            RowLayout {
                Layout.fillWidth:true
                TextField {id:presetName;placeholderText:qsTranslate("Luma","Metadata preset name");maximumLength:128;Layout.fillWidth:true}
                Button {text:qsTranslate("Luma","Save preset");enabled:presetName.text.trim().length>0;onClicked:library.saveImportMetadataPreset(presetName.text,dialog.metadataValues())}
            }
            Label {visible:mode.currentIndex===2;text:qsTranslate("Luma","Move deletes each source only after the imported file, optional backup and catalog entry are complete. Duplicate sources are kept.");wrapMode:Text.Wrap;Layout.fillWidth:true}
            Label {visible:library.error.length>0;text:qsTranslate("Luma",library.error);wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#ffc1bc"}
            Label {visible:presets.error.length>0;text:qsTranslate("Luma",presets.error);wrapMode:Text.Wrap;Layout.fillWidth:true;color:"#ffc1bc"}
        }
    }
    footer:DialogButtonBox {
        Button {objectName:"library.importCancel";text:qsTranslate("Luma","Cancel");onClicked:dialog.reject()}
        Button {
            objectName:"library.importStart";text:qsTranslate("Luma","Import photos")
            enabled:dialog.sources.length>0&&(mode.currentIndex===0||dialog.destination.toString().length>0)&&!library.working
            onClicked: {
                const preset=develop.currentValue?presets.importSnapshot(develop.currentValue):""
                if(develop.currentValue&&!preset)return
                if(library.importAdvanced(dialog.sources,{mode:mode.currentIndex,destination:dialog.destination,backup:dialog.backup,recursive:recursive.checked,skipDuplicates:duplicates.checked,dateFolders:mode.currentIndex!==0&&dates.checked,naming:mode.currentIndex===0?"{name}":naming.text,metadata:dialog.metadataValues()},preset))dialog.close()
            }
        }
    }
}
