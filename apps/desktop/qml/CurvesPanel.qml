import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: panel
    objectName: "curves.panel"
    property string channel: channels.currentValue ?? "rgb"
    property var points: editor.curvePoints[channel] ?? []
    property var samples: []
    property int selected: -1
    property bool available: editor.hasPhoto && !editor.busy && editor.correctionStates.curves
    spacing: 8
    function refresh() {
        samples = editor.curveSamples(channel, mode.currentIndex === 1);
        graph.requestPaint();
    }
    onChannelChanged: {
        if (curveGesture && curveGesture.dragging) {
            curveGesture.dragging = false;
            editor.endEdit();
        }
        selected = -1;
        refresh();
        if (editor.targetedTool.startsWith("curve."))
            editor.targetedTool = "curve." + channel;
    }
    onPointsChanged: {
        if (curveGesture.dragging && points.length !== curveGesture.pointCount) {
            curveGesture.dragging = false;
            selected = -1;
            editor.endEdit();
        }
    }
    Connections {
        target: editor
        function onPhotoLoaded() {
            curveGesture.dragging = false;
            panel.selected = -1;
            graph.requestPaint();
        }
        function onStateChanged() {
            panel.refresh();
        }
    }
    Component.onCompleted: refresh()
    RowLayout {
        CheckBox {
            checked: editor.correctionStates.curves
            enabled: editor.hasPhoto && !editor.busy
            onToggled: editor.setCorrectionEnabled("curves", checked)
            padding: 0
            Accessible.name: qsTranslate("Luma", "Enable curves")
        }
        Label {
            text: qsTranslate("Luma", "CURVES")
            color: "#858c97"
            font.pixelSize: 9
            font.letterSpacing: 2
            Layout.fillWidth: true
        }
    }
    ComboBox {
        id: mode
        objectName: "curves.mode"
        model: [qsTranslate("Luma", "Point"), qsTranslate("Luma", "Parametric")]
        Layout.fillWidth: true
        enabled: panel.available
        onActivated: panel.refresh()
        Accessible.name: qsTranslate("Luma", "Curve mode")
    }
    RowLayout {
        visible: mode.currentIndex === 0
        ComboBox {
            id: channels
            objectName: "curves.channel"
            Layout.fillWidth: true
            enabled: panel.available
            textRole: "label"
            valueRole: "value"
            model: [
                {
                    label: "RGB",
                    value: "rgb"
                },
                {
                    label: qsTranslate("Luma", "Red"),
                    value: "red"
                },
                {
                    label: qsTranslate("Luma", "Green"),
                    value: "green"
                },
                {
                    label: qsTranslate("Luma", "Blue"),
                    value: "blue"
                }
            ]
            Accessible.name: qsTranslate("Luma", "Curve channel")
        }
        Button {
            objectName: "curves.target"
            text: "◎"
            checkable: true
            checked: editor.targetedTool === "curve." + panel.channel
            enabled: panel.available && !editor.original
            onClicked: editor.targetedTool = checked ? "curve." + panel.channel : "none"
            Accessible.name: qsTranslate("Luma", "Targeted Curve")
            ToolTip.visible: hovered
            ToolTip.text: qsTranslate("Luma", "Adjust tone with a photo gesture")
        }
        Button {
            text: qsTranslate("Luma", "Reset")
            enabled: panel.available
            onClicked: {
                panel.selected = -1;
                editor.resetCurve(panel.channel);
            }
            Accessible.name: qsTranslate("Luma", "Reset channel curve")
        }
    }
    Canvas {
        id: graph
        objectName: "curves.graph"
        Layout.fillWidth: true
        Layout.preferredHeight: width
        opacity: panel.available ? 1 : 0.5
        focus: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            ctx.fillStyle = "#12151b";
            ctx.fillRect(0, 0, width, height);
            ctx.strokeStyle = "#2c323c";
            ctx.lineWidth = 1;
            for (let i = 1; i < 4; ++i) {
                ctx.beginPath();
                ctx.moveTo(i * width / 4, 0);
                ctx.lineTo(i * width / 4, height);
                ctx.moveTo(0, i * height / 4);
                ctx.lineTo(width, i * height / 4);
                ctx.stroke();
            }
            ctx.strokeStyle = "#424955";
            ctx.beginPath();
            ctx.moveTo(0, height);
            ctx.lineTo(width, 0);
            ctx.stroke();
            ctx.strokeStyle = panel.channel === "red" ? "#e27272" : panel.channel === "green"
                                                        ? "#80c590" : panel.channel === "blue"
                                                          ? "#8baff4" : "#dfae77";
            ctx.lineWidth = 2;
            ctx.beginPath();
            for (let i = 0; i < panel.samples.length; ++i) {
                const x = i * width / (panel.samples.length - 1), y = (1 - panel.samples[i])
                                                                  * height;


                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.stroke();
            if (mode.currentIndex === 0)
                for (let i = 0; i < panel.points.length; ++i) {
                    const p = panel.points[i];
                    ctx.fillStyle = i === panel.selected ? "#f5dab6" : "#b7bfcd";
                    ctx.beginPath();
                    ctx.arc(p.x * width, (1 - p.y) * height, 4, 0, Math.PI * 2);
                    ctx.fill();
                }
        }
        Keys.onDeletePressed: if (panel.selected > 0 && panel.selected < panel.points.length - 1) {
                                  editor.removeCurvePoint(panel.channel, panel.selected);
                                  panel.selected = -1;
                                  graph.requestPaint();
                              }
        MouseArea {
            id: curveGesture
            objectName: "curves.gesture"
            anchors.fill: parent
            preventStealing: true
            enabled: panel.available && mode.currentIndex === 0
            property bool dragging: false
            property int pointCount: 0
            onPressed: function (mouse) {
                graph.forceActiveFocus();
                panel.selected = -1;
                let distance = 14;
                for (let i = 0; i < panel.points.length; ++i) {
                    const p = panel.points[i], d = Math.hypot(mouse.x - p.x * width, mouse.y - (1
                                                                                                - p.y) * height);
                    if (d < distance) {
                        distance = d;
                        panel.selected = i;
                    }
                }
                editor.beginEdit();
                if (panel.selected < 0) {
                    const x = Math.max(0.001, Math.min(0.999, mouse.x / width));
                    for (let i = 0; i < panel.points.length; ++i)
                        if (Math.abs(panel.points[i].x - x) < 0.001)
                            panel.selected = i;
                    if (panel.selected < 0)
                        panel.selected = editor.addCurvePoint(panel.channel, x, Math.max(0, Math.min(
                                                                                             1, 1 - mouse.y
                                                                                             / height)));
                }
                pointCount = panel.points.length;
                dragging = panel.selected >= 0;
                graph.requestPaint();
            }
            onPositionChanged: function (mouse) {
                if (pressed && dragging)
                    editor.moveCurvePoint(panel.channel, panel.selected, mouse.x / width, 1
                                          - mouse.y / height);
            }
            onReleased: {
                dragging = false;
                editor.endEdit();
            }
            onCanceled: {
                dragging = false;
                editor.endEdit();
            }
        }
    }
    RowLayout {
        visible: mode.currentIndex === 0
        Label {
            text: qsTranslate("Luma", "Click to add a point; drag to adjust")
            font.pixelSize: 10
            color: "#858c97"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Button {
            text: "−"
            enabled: panel.available && panel.selected > 0 && panel.selected < panel.points.length
                     - 1

            onClicked: {
                editor.removeCurvePoint(panel.channel, panel.selected);
                panel.selected = -1;
                graph.requestPaint();
            }
            Accessible.name: qsTranslate("Luma", "Delete curve point")
        }
    }
    ColumnLayout {
        visible: mode.currentIndex === 1
        Layout.fillWidth: true
        DevelopSlider {
            parameter: "curve.highlights"
            label: qsTranslate("Luma", "Highlights")
            correction: "curves"
            allowDisable: false
            Layout.fillWidth: true
        }
        DevelopSlider {
            parameter: "curve.lights"
            label: qsTranslate("Luma", "Lights")
            correction: "curves"
            allowDisable: false
            Layout.fillWidth: true
        }
        DevelopSlider {
            parameter: "curve.darks"
            label: qsTranslate("Luma", "Darks")
            correction: "curves"
            allowDisable: false
            Layout.fillWidth: true
        }
        DevelopSlider {
            parameter: "curve.shadows"
            label: qsTranslate("Luma", "Shadows")
            correction: "curves"
            allowDisable: false
            Layout.fillWidth: true
        }
    }
}
