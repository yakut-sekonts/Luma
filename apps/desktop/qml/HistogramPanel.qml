import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "histogram.panel"
    spacing: 8
    Layout.minimumWidth: 0
    readonly property var histogramData: editor.histogram
    readonly property bool available: editor.hasPhoto && !editor.busy && !editor.editingLocked && !editor.original && !editor.exporting
    readonly property var parameters: ["blacks", "shadows", "exposure", "highlights", "whites"]
    readonly property var labels: [qsTranslate("Luma", "Blacks"), qsTranslate("Luma", "Shadows"), qsTranslate("Luma", "Exposure"), qsTranslate("Luma", "Highlights"), qsTranslate("Luma", "Whites")]
    property int selectedZone: 2
    readonly property string parameter: parameters[selectedZone]
    readonly property bool controlEnabled: available && (editor.correctionStates[parameter] ?? false)
    function finishGesture() {
        if (!chartInput || !chartInput.dragging) return
        chartInput.dragging = false
        editor.endHistogramEdit()
    }
    function nudge(direction) {
        if (!controlEnabled || !editor.beginHistogramEdit(parameter)) return
        editor.updateHistogramEdit(direction * (parameter === "exposure" ? 0.01 : 1))
        editor.endHistogramEdit()
    }
    onAvailableChanged: { if (!available) finishGesture() }
    onSelectedZoneChanged: { if (graph) graph.requestPaint() }
    Connections {
        target: editor
        function onHistogramChanged() { if (graph) graph.requestPaint() }
        function onPhotoLoaded() { if (chartInput) chartInput.dragging = false }
    }
    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTranslate("Luma", "HISTOGRAM"); color: "#858c97"; font.pixelSize: 9; font.letterSpacing: 1; Layout.fillWidth: true }
        ToolButton {
            id: helpToggle
            objectName: "histogram.help"
            text: "?"; checkable: true
            Accessible.name: qsTranslate("Luma", "Histogram help")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            id: mode
            objectName: "histogram.mode"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: ["RGB", qsTranslate("Luma", "Luminance")]
            Accessible.name: qsTranslate("Luma", "Histogram channels")
            onCurrentIndexChanged: { if (graph) graph.requestPaint() }
        }
        CheckBox {
            id: logarithmic
            objectName: "histogram.logScale"
            text: qsTranslate("Luma", "Log scale")
            onCheckedChanged: { if (graph) graph.requestPaint() }
        }
    }
    Canvas {
        id: graph
        objectName: "histogram.graph"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredHeight: 110
        activeFocusOnTab: true
        Accessible.name: qsTranslate("Luma", "Histogram")
        Accessible.description: qsTranslate("Luma", "Use the tone selector and minus/plus buttons, or Left/Right keys, to adjust tones.")
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Keys.onLeftPressed: function(event) { panel.nudge(event.modifiers & Qt.ShiftModifier ? -10 : -1); event.accepted = true }
        Keys.onRightPressed: function(event) { panel.nudge(event.modifiers & Qt.ShiftModifier ? 10 : 1); event.accepted = true }
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "#111318"
            ctx.fillRect(0, 0, width, height)
            ctx.fillStyle = "#242b35"
            ctx.fillRect(panel.selectedZone * width / 5, 0, width / 5, height)
            ctx.lineWidth = 1
            ctx.strokeStyle = activeFocus ? "#dfae77" : "#39414c"
            ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
            for (let i = 1; i < 5; ++i) {
                ctx.beginPath(); ctx.moveTo(i * width / 5, 0); ctx.lineTo(i * width / 5, height); ctx.stroke()
            }
            if (!(panel.histogramData.weight > 0)) return
            const names = mode.currentIndex === 0 ? ["red", "green", "blue"] : ["luminance"]
            const colors = mode.currentIndex === 0 ? ["#ff7c78", "#83d18b", "#78aaff"] : ["#d9dce2"]
            let peak = 0
            for (let c = 0; c < names.length; ++c) {
                const values = panel.histogramData[names[c]] || []
                for (let i = 0; i < values.length; ++i) peak = Math.max(peak, values[i])
            }
            if (peak <= 0) return
            const denominator = logarithmic.checked ? Math.log(1 + peak) : peak
            for (let c = 0; c < names.length; ++c) {
                const values = panel.histogramData[names[c]] || []
                ctx.beginPath(); ctx.moveTo(1, height - 1)
                for (let i = 0; i < values.length; ++i) {
                    const value = logarithmic.checked ? Math.log(1 + values[i]) : values[i]
                    ctx.lineTo(1 + i * (width - 2) / 255, height - 1 - value / denominator * (height - 3))
                }
                ctx.lineTo(width - 1, height - 1); ctx.closePath()
                ctx.fillStyle = colors[c]; ctx.globalAlpha = 0.18; ctx.fill()
                ctx.strokeStyle = colors[c]; ctx.globalAlpha = 0.9; ctx.stroke()
            }
            ctx.globalAlpha = 1
        }
        onActiveFocusChanged: requestPaint()
        Label {
            anchors.centerIn: parent
            width: parent.width - 16
            text: editor.histogramPending ? qsTranslate("Luma", "Updating histogram…") : qsTranslate("Luma", "No histogram samples")
            visible: !(panel.histogramData.weight > 0)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 11
        }
        MouseArea {
            id: chartInput
            objectName: "histogram.dragArea"
            anchors.fill: parent
            enabled: panel.available
            acceptedButtons: Qt.LeftButton
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            property bool dragging: false
            property real startX: 0
            property real dragRange: 200
            onPressed: function(mouse) {
                panel.selectedZone = Math.max(0, Math.min(4, Math.floor(mouse.x * 5 / Math.max(1, width))))
                dragging = editor.beginHistogramEdit(panel.parameter)
                if (!dragging) { mouse.accepted = false; return }
                startX = mouse.x
                dragRange = panel.parameter === "exposure" ? 10 : 200
                graph.forceActiveFocus()
            }
            onPositionChanged: function(mouse) {
                if (pressed && dragging) editor.updateHistogramEdit((mouse.x - startX) / Math.max(1, width) * dragRange)
            }
            onReleased: panel.finishGesture()
            onCanceled: panel.finishGesture()
        }
    }
    Label {
        text: editor.histogramPending ? qsTranslate("Luma", "Updating histogram…") : qsTranslate("Luma", "Sampled preview · sRGB")
        font.pixelSize: 10; color: "#858c97"; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    Label {
        text: qsTranslate("Luma", "Drag left/right in a tone zone. Zones run from Blacks to Whites.")
        visible: helpToggle.checked
        font.pixelSize: 10; color: "#858c97"; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    RowLayout {
        Layout.fillWidth: true
        ComboBox {
            objectName: "histogram.tone"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: panel.labels
            currentIndex: panel.selectedZone
            onActivated: { panel.finishGesture(); panel.selectedZone = currentIndex }
            Accessible.name: qsTranslate("Luma", "Histogram tone control")
        }
        Label {
            text: Number(editor.parameters[panel.parameter] ?? 0).toFixed(panel.parameter === "exposure" ? 2 : 0)
            color: "#dfae77"; textFormat: Text.PlainText
        }
        Button {
            objectName: "histogram.decrease"
            text: "−"; implicitWidth: 32
            enabled: panel.controlEnabled
            Accessible.name: qsTranslate("Luma", "Decrease %1").arg(panel.labels[panel.selectedZone])
            onClicked: panel.nudge(-1)
        }
        Button {
            objectName: "histogram.increase"
            text: "+"; implicitWidth: 32
            enabled: panel.controlEnabled
            Accessible.name: qsTranslate("Luma", "Increase %1").arg(panel.labels[panel.selectedZone])
            onClicked: panel.nudge(1)
        }
    }
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        CheckBox {
            objectName: "histogram.shadowClipping"
            text: qsTranslate("Luma", "Shadows")
            Accessible.name: qsTranslate("Luma", "Shadow clipping")
            checked: editor.shadowClipping
            enabled: editor.hasPhoto && !editor.busy
            onToggled: editor.shadowClipping = checked
            Layout.fillWidth: true
        }
        CheckBox {
            objectName: "histogram.highlightClipping"
            text: qsTranslate("Luma", "Highlights")
            Accessible.name: qsTranslate("Luma", "Highlight clipping")
            checked: editor.highlightClipping
            enabled: editor.hasPhoto && !editor.busy
            onToggled: editor.highlightClipping = checked
            Layout.fillWidth: true
        }
    }
    Label {
        text: qsTranslate("Luma", "Shadows ≈ %1% · Highlights ≈ %2%")
            .arg(Number(panel.histogramData.shadowsPercent ?? 0).toLocaleString(Qt.locale(), 'f', 2))
            .arg(Number(panel.histogramData.highlightsPercent ?? 0).toLocaleString(Qt.locale(), 'f', 2))
        visible: panel.histogramData.weight > 0
        font.pixelSize: 10; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    Label {
        text: qsTranslate("Luma", "Red marks clipped highlights; blue marks clipped shadows. Hold Option while adjusting Whites or Blacks for a temporary overlay.")
        visible: helpToggle.checked || editor.previewClipping !== 0
        font.pixelSize: 10; color: "#858c97"; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    Label {
        text: qsTranslate("Luma", "Clipping is measured against sRGB output, not RAW sensor saturation. Transparent areas are excluded; overlays are never exported.")
        visible: helpToggle.checked
        font.pixelSize: 10; color: "#858c97"; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
    Label {
        text: qsTranslate("Luma", "Histogram and clipping overlays are unavailable in Detail diagnostic views.")
        visible: editor.detailDiagnostic.length > 0
        font.pixelSize: 10; color: "#dfae77"; wrapMode: Text.Wrap; Layout.fillWidth: true
    }
}
