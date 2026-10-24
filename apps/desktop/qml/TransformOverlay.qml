import QtQuick

Item {
    id: overlay
    property rect paintBounds: Qt.rect(0,0,width,height)
    objectName: "transformOverlay"
    visible: editor.guidedMode && editor.guidedCanvasReady
    property var guides: editor.transformGuides
    Canvas {
        id: canvas
        objectName: "TransformOverlay.canvas"
        x: overlay.paintBounds.x; y: overlay.paintBounds.y
        width: Math.max(1, overlay.paintBounds.width); height: Math.max(1, overlay.paintBounds.height)
        onXChanged: requestPaint()
        onYChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0,0,canvas.width,canvas.height)
            const width=overlay.width, height=overlay.height
            ctx.save();ctx.translate(-canvas.x,-canvas.y)
            for (let i=0; i<overlay.guides.length; ++i) {
                const line=overlay.guides[i]
                ctx.strokeStyle=line.vertical ? "#53d6e4" : "#ffbc72"
                ctx.lineWidth=2
                ctx.beginPath(); ctx.moveTo(line.x1*width,line.y1*height); ctx.lineTo(line.x2*width,line.y2*height); ctx.stroke()
            }
            ctx.restore()
        }
    }
    Connections { target: editor; function onPreviewChanged() { canvas.requestPaint() } }
    Repeater {
        model: 4
        Item {
            id: lineItem
            required property int index
            width: overlay.width; height: overlay.height
            property var guide: overlay.guides[index] || ({})
            visible: index < overlay.guides.length
            Repeater {
                model: 2
                Rectangle {
                    id: grip
                    required property int index
                    objectName: "transformGuidePoint" + lineItem.index + "_" + index
                    x: (index===0 ? lineItem.guide.x1 || 0 : lineItem.guide.x2 || 0) * overlay.width - width/2
                    y: (index===0 ? lineItem.guide.y1 || 0 : lineItem.guide.y2 || 0) * overlay.height - height/2
                    width: 16; height: 16; radius: 8
                    color: lineItem.guide.vertical ? "#53d6e4" : "#ffbc72"
                    border.color: "#14202a"; border.width: 2
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.CrossCursor
                        enabled: overlay.visible && !editor.busy && !editor.editingLocked
                        onPositionChanged: function(mouse) {
                            if (!pressed || overlay.width<=0 || overlay.height<=0) return
                            const p=mapToItem(overlay,mouse.x,mouse.y)
                            editor.moveGuidePoint(lineItem.index,grip.index,Math.max(0,Math.min(1,p.x/overlay.width)),Math.max(0,Math.min(1,p.y/overlay.height)))
                        }
                    }
                }
            }
        }
    }
}
