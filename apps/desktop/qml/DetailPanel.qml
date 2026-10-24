import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    objectName: "detailPanel"
    spacing: 8
    RowLayout {
        Layout.fillWidth: true
        Label { objectName: "detail.heading"; text: qsTranslate("Luma", "DETAIL / SHARPENING"); color: "#858c97"; font.pixelSize: 10; font.letterSpacing: 1.5; Layout.fillWidth: true }
        CheckBox { objectName: "sharpenEnabled"; checked: editor.correctionStates.sharpening; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("sharpening", checked); Accessible.name: qsTranslate("Luma", "Enable sharpening") }
    }
    DevelopSlider { objectName: "sharpenAmount"; parameter: "sharpen.amount"; label: qsTranslate("Luma", "Amount"); minimum: 0; maximum: 150; correction: "sharpening"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "sharpenRadius"; parameter: "sharpen.radius"; label: qsTranslate("Luma", "Radius"); minimum: 0.5; maximum: 3; step: 0.05; defaultValue: 1; correction: "sharpening"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "sharpenDetail"; parameter: "sharpen.detail"; label: qsTranslate("Luma", "Detail"); minimum: 0; maximum: 100; defaultValue: 25; correction: "sharpening"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "sharpenMasking"; parameter: "sharpen.masking"; label: qsTranslate("Luma", "Masking"); minimum: 0; maximum: 100; correction: "sharpening"; allowDisable: false; detailDiagnostic: true }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTranslate("Luma", "LUMINANCE NOISE"); color: "#858c97"; font.pixelSize: 10; Layout.fillWidth: true }
        CheckBox { objectName: "luminanceNoiseEnabled"; checked: editor.correctionStates.luminanceNoise; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("luminanceNoise", checked); Accessible.name: qsTranslate("Luma", "Enable luminance noise reduction") }
    }
    DevelopSlider { objectName: "noiseluminance"; parameter: "noise.luminance"; label: qsTranslate("Luma", "Amount"); minimum: 0; maximum: 100; defaultValue: 0; correction: "luminanceNoise"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "noiseluminanceDetail"; parameter: "noise.luminanceDetail"; label: qsTranslate("Luma", "Detail"); minimum: 0; maximum: 100; defaultValue: 50; correction: "luminanceNoise"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "noiseluminanceContrast"; parameter: "noise.luminanceContrast"; label: qsTranslate("Luma", "Contrast"); minimum: 0; maximum: 100; defaultValue: 0; correction: "luminanceNoise"; allowDisable: false; detailDiagnostic: true }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTranslate("Luma", "COLOR NOISE"); color: "#858c97"; font.pixelSize: 10; Layout.fillWidth: true }
        CheckBox { objectName: "colorNoiseEnabled"; checked: editor.correctionStates.colorNoise; enabled: editor.hasPhoto && !editor.busy; onToggled: editor.setCorrectionEnabled("colorNoise", checked); Accessible.name: qsTranslate("Luma", "Enable color noise reduction") }
    }
    DevelopSlider { objectName: "noisecolor"; parameter: "noise.color"; label: qsTranslate("Luma", "Amount"); minimum: 0; maximum: 100; defaultValue: 0; correction: "colorNoise"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "noisecolorDetail"; parameter: "noise.colorDetail"; label: qsTranslate("Luma", "Detail"); minimum: 0; maximum: 100; defaultValue: 50; correction: "colorNoise"; allowDisable: false; detailDiagnostic: true }
    DevelopSlider { objectName: "noisecolorSmoothness"; parameter: "noise.colorSmoothness"; label: qsTranslate("Luma", "Smoothness"); minimum: 0; maximum: 100; defaultValue: 50; correction: "colorNoise"; allowDisable: false; detailDiagnostic: true }
    Label { text: qsTranslate("Luma", "Option + drag shows diagnostics. Radius uses original-image pixels."); wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10; color: "#858c97" }
    Label { objectName: "denoise.heading"; text: qsTranslate("Luma", "AI Denoise"); Layout.fillWidth: true }
    Label {
        text: editor.denoiseModelConfigured
            ? qsTranslate("Luma", "Creates a new unedited Linear DNG from the RAW. Processing stays on this Mac.")
            : qsTranslate("Luma", "Choose a compatible local model in Settings to use AI Denoise.")
        wrapMode: Text.Wrap; Layout.fillWidth: true; font.pixelSize: 10; color: "#858c97"
    }
    Slider {
        id: denoiseAmount
        objectName: "denoise.amount"
        from: 1; to: 100; value: 100
        enabled: !editor.aiBusy
        Layout.fillWidth: true
        Accessible.name: qsTranslate("Luma", "AI Denoise amount")
    }
    Button {
        objectName: "denoise.create"
        text: qsTranslate("Luma", "Create denoised DNG") + " · " + Math.round(denoiseAmount.value) + "%"
        enabled: editor.denoiseModelConfigured && editor.hasPhoto && !editor.busy && !editor.aiBusy && !editor.editingLocked
        Layout.fillWidth: true
        onClicked: editor.denoisePhoto(denoiseAmount.value)
    }
    ProgressBar { objectName: "denoise.progress"; visible: editor.aiBusy && editor.aiOperation === "AI Denoise"; value: editor.aiProgress; Layout.fillWidth: true }
    Button { objectName: "denoise.cancel"; visible: editor.aiBusy && editor.aiOperation === "AI Denoise"; text: qsTranslate("Luma", "Cancel"); onClicked: editor.cancelAI(); Layout.fillWidth: true }
}
