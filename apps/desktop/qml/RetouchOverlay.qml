import QtQuick

Item {
    id: overlay
    visible: editor.hasPhoto && editor.retouchMode !== "off" && !editor.original
    property var shape: editor.retouchOverlay
    onShapeChanged: drawing.requestPaint()
    onWidthChanged: drawing.requestPaint()
    onHeightChanged: drawing.requestPaint()
    Canvas {
        id: drawing
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const points = overlay.shape.points || []
            if (points.length) {
                ctx.beginPath()
                ctx.moveTo(points[0].x * width, points[0].y * height)
                for (let i = 1; i < points.length; ++i)
                    ctx.lineTo(points[i].x * width, points[i].y * height)
                // A single click is a circular dab.
                if (points.length === 1)
                    ctx.lineTo(points[0].x * width + .01, points[0].y * height)
                ctx.lineWidth = Math.max(1, 2 * overlay.shape.radius * width)
                ctx.strokeStyle = "#50dfae77"
                ctx.lineCap = "round"; ctx.lineJoin = "round"
                ctx.stroke()
            }
            const source = overlay.shape.source
            if (source) {
                ctx.beginPath()
                ctx.arc(source.x * width, source.y * height, 7, 0, 2 * Math.PI)
                ctx.lineWidth = 1.5; ctx.strokeStyle = "#ffffff"; ctx.stroke()
                if (points.length) {
                    ctx.beginPath()
                    ctx.moveTo(source.x * width, source.y * height)
                    ctx.lineTo(points[0].x * width, points[0].y * height)
                    ctx.stroke()
                }
            }
        }
    }
    MouseArea {
        objectName: "retouch.gesture"
        anchors.fill: parent
        enabled: !editor.busy && !editor.retouchBusy && !editor.editingLocked && width > 0 && height > 0
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.LeftButton
        property bool drawingStroke: false
        onPressed: function(mouse) {
            if (mouse.modifiers & Qt.AltModifier) {
                editor.pickRetouchSource(mouse.x / width, mouse.y / height)
                drawingStroke = false
            } else {
                editor.beginRetouch(mouse.x / width, mouse.y / height)
                drawingStroke = editor.retouchState.drawing
            }
        }
        onPositionChanged: function(mouse) {
            if (pressed && drawingStroke)
                editor.continueRetouch(mouse.x / width, mouse.y / height)
        }
        onReleased: { if (drawingStroke) editor.endRetouch(); drawingStroke = false }
        onCanceled: { editor.cancelRetouch(); drawingStroke = false }
    }
}
