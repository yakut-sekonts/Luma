import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "transformPanel"
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTranslate("Luma", "TRANSFORM"); color: "#858c97"; font.pixelSize: 10; font.letterSpacing: 1.5; Layout.fillWidth: true }
        CheckBox { objectName: "transformEnabled"; checked: editor.correctionStates.transform; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("transform", checked); Accessible.name: qsTranslate("Luma", "Enable Transform") }
    }
    RowLayout {
        Layout.fillWidth: true
        Button { objectName: "transformAuto"; text: editor.transformAutoBusy ? qsTranslate("Luma", "Finding lines…") : qsTranslate("Luma", "Auto"); enabled: editor.hasPhoto && !editor.busy && !editor.original && !editor.transformAutoBusy; Layout.fillWidth: true; onClicked: editor.autoTransform() }
        Button { objectName: "transformAutoCancel"; visible: editor.transformAutoBusy; text: qsTranslate("Luma", "Cancel"); onClicked: editor.cancelAutoTransform() }
        Button { objectName: "transformGuided"; text: qsTranslate("Luma", "Guided"); checkable: true; checked: editor.guidedMode; enabled: editor.hasPhoto && !editor.busy && !editor.original; Layout.fillWidth: true; onToggled: editor.guidedMode = checked }
    }
    ColumnLayout {
        visible: editor.guidedMode
        Layout.fillWidth: true
        Label { text: qsTranslate("Luma", "Align guides with straight lines in the original photo."); wrapMode: Text.Wrap; color: "#c9d2dd"; font.pixelSize: 11; Layout.fillWidth: true }
        ComboBox { objectName: "transformGuideLayout"; Layout.fillWidth: true; model: [qsTranslate("Luma", "2 vertical"), qsTranslate("Luma", "2 horizontal"), qsTranslate("Luma", "Vertical + horizontal")]; currentIndex: editor.transformGuideLayout; onActivated: function(index) { editor.setGuideLayout(index) }; Accessible.name: qsTranslate("Luma", "Guided correction directions") }
        Button { objectName: "transformGuideApply"; text: qsTranslate("Luma", "Apply guides"); enabled: editor.guidedCanvasReady; Layout.fillWidth: true; onClicked: editor.applyGuides() }
    }
    ColumnLayout {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.maximumWidth: panel.width
        enabled: !editor.guidedMode
        DevelopSlider { objectName: "transformRotate"; parameter: "transform.rotate"; label: qsTranslate("Luma", "Rotate"); minimum: -45; maximum: 45; step: 0.05; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformHorizontal"; parameter: "transform.horizontal"; label: qsTranslate("Luma", "Horizontal"); minimum: -100; maximum: 100; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformVertical"; parameter: "transform.vertical"; label: qsTranslate("Luma", "Vertical"); minimum: -100; maximum: 100; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformAspect"; parameter: "transform.aspect"; label: qsTranslate("Luma", "Aspect"); minimum: -100; maximum: 100; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformScale"; parameter: "transform.scale"; label: qsTranslate("Luma", "Scale"); minimum: 50; maximum: 200; defaultValue: 100; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformXOffset"; parameter: "transform.xOffset"; label: qsTranslate("Luma", "X Offset"); minimum: -100; maximum: 100; correction: "transform"; allowDisable: false }
        DevelopSlider { objectName: "transformYOffset"; parameter: "transform.yOffset"; label: qsTranslate("Luma", "Y Offset"); minimum: -100; maximum: 100; correction: "transform"; allowDisable: false }
        CheckBox {
            id: constrain
            objectName: "transformConstrain"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            text: qsTranslate("Luma", "Constrain Crop · remove empty edges")
            checked: !!editor.parameters["transform.constrainCrop"]
            enabled: editor.hasPhoto && !editor.busy
            onToggled: editor.setParameter("transform.constrainCrop", checked ? 1 : 0)
            contentItem: Label {
                text: constrain.text; font: constrain.font; color: constrain.palette.buttonText
                verticalAlignment: Text.AlignVCenter; wrapMode: Text.Wrap
                leftPadding: constrain.indicator && !constrain.mirrored ? constrain.indicator.width + constrain.spacing : 0
                rightPadding: constrain.indicator && constrain.mirrored ? constrain.indicator.width + constrain.spacing : 0
            }
        }
    }
    Label { visible: !!editor.transformState.guided; text: qsTranslate("Luma", "Auto/Guided matrix applied; manual controls adjust it further."); wrapMode: Text.Wrap; Layout.fillWidth: true; color: "#c9d2dd"; font.pixelSize: 10 }
    Label { text: qsTranslate("Luma", "Actual scale: ") + Number(editor.transformState.effectiveScale || 100).toFixed(1) + "%"; color: "#858c97"; font.pixelSize: 10 }
    Label { text: qsTranslate("Luma", "Auto and Guided replace Transform in one Undo step. Crop is preserved. Unconstrained edges are transparent; JPEG uses black."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10; color: "#858c97" }
    Button { objectName: "transformReset"; text: qsTranslate("Luma", "Reset Transform"); enabled: editor.hasPhoto && !editor.busy; Layout.fillWidth: true; onClicked: editor.resetTransform() }
}
