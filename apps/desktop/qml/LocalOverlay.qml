import QtQuick
Item {
    id: overlay
    property rect paintBounds: Qt.rect(0,0,width,height)
    objectName: "localPhotoOverlay"
    visible: editor.localMode && editor.hasPhoto && !editor.original
    clip: true
    property real photoWidth: width
    property real photoHeight: height
    readonly property real photoX: (width-photoWidth)/2
    readonly property real photoY: (height-photoHeight)/2
    property var rawHandles: editor.localHandles
    property var handles: layoutHandles()
    property var brushOutline: []
    property real hoverX: -1
    property real hoverY: -1

    function insidePhoto(x,y) {
        return photoWidth>0&&photoHeight>0&&x>=photoX&&y>=photoY&&
                x<=photoX+photoWidth&&y<=photoY+photoHeight
    }
    function photoPoint(x,y,bounded) {
        const px=(x-photoX)/photoWidth, py=(y-photoY)/photoHeight
        return bounded?{x:Math.max(0,Math.min(1,px)),y:Math.max(0,Math.min(1,py))}:{x:px,y:py}
    }
    function layoutHandles() {
        if(rawHandles.length!==3||photoWidth<=0||photoHeight<=0||width<84||height<84)return []
        const inset=8, separation=16, result=[]
        for(let i=0;i<3;++i) {
            const raw=rawHandles[i]
            if(raw.handle!==i||!Number.isFinite(raw.x)||!Number.isFinite(raw.y))return []
            const rx=photoX+raw.x*photoWidth, ry=photoY+raw.y*photoHeight
            if(!Number.isFinite(rx)||!Number.isFinite(ry))return []
            const preferred={x:Math.max(inset,Math.min(width-inset,rx)),
                             y:Math.max(inset,Math.min(height-inset,ry))}
            const candidates=[preferred]
            for(let dy=-1;dy<=1;++dy)for(let dx=-1;dx<=1;++dx)
                if(dx!==0||dy!==0)candidates.push({x:preferred.x+dx*separation,y:preferred.y+dy*separation})
            // At min 84px these nine centers are >=34px apart. Two prior
            // handles can each exclude at most one grid center at radius16,
            // so a distinct position always exists, including tiny photos.
            for(let gy=0;gy<3;++gy)for(let gx=0;gx<3;++gx)
                candidates.push({x:inset+gx*(width-2*inset)/2,y:inset+gy*(height-2*inset)/2})
            let chosen=null, distance=Infinity
            for(const candidate of candidates) {
                if(candidate.x<inset||candidate.x>width-inset||candidate.y<inset||candidate.y>height-inset)continue
                let available=true
                for(const prior of result) {
                    const dx=candidate.x-prior.x*width, dy=candidate.y-prior.y*height
                    if(dx*dx+dy*dy<separation*separation){available=false;break}
                }
                if(!available)continue
                const dx=candidate.x-preferred.x,dy=candidate.y-preferred.y,d=dx*dx+dy*dy
                if(d<distance){chosen=candidate;distance=d}
            }
            // The min-size/grid invariant above guarantees this branch cannot
            // occur for finite valid handles; never merge handle identities.
            if(chosen===null)return []
            result.push({x:chosen.x/width,y:chosen.y/height,handle:raw.handle,
                         // Empty Transform edges need the same unbounded inverse as displaced handles.
                         proxy:raw.covered===false||chosen.x!==rx||chosen.y!==ry})
        }
        return result
    }
    function refreshBrush() {brushOutline=hoverX>=0?editor.localBrushOutline(hoverX,hoverY):[];canvas.requestPaint()}
    onHandlesChanged: if(canvas)canvas.requestPaint()
    Canvas {
        id: canvas
        objectName: "LocalOverlay.canvas"
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
            if (overlay.handles.length === 3) {
            // Guides describe the stored mask, not the relocated hit targets.
            ctx.save();ctx.beginPath();ctx.rect(overlay.photoX,overlay.photoY,overlay.photoWidth,overlay.photoHeight);ctx.clip()
            ctx.strokeStyle = "white"; ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.moveTo(overlay.photoX+overlay.rawHandles[0].x*overlay.photoWidth,overlay.photoY+overlay.rawHandles[0].y*overlay.photoHeight)
            ctx.lineTo(overlay.photoX+overlay.rawHandles[1].x*overlay.photoWidth,overlay.photoY+overlay.rawHandles[1].y*overlay.photoHeight)
            ctx.moveTo(overlay.photoX+overlay.rawHandles[0].x*overlay.photoWidth,overlay.photoY+overlay.rawHandles[0].y*overlay.photoHeight)
            ctx.lineTo(overlay.photoX+overlay.rawHandles[2].x*overlay.photoWidth,overlay.photoY+overlay.rawHandles[2].y*overlay.photoHeight)
            ctx.stroke();ctx.restore()
            for(const handle of overlay.handles) {
                ctx.fillStyle = "#dfae77";ctx.strokeStyle=handle.proxy?"#dfae77":"white";ctx.lineWidth=1.5;ctx.beginPath()
                ctx.arc(handle.x*width,handle.y*height,6,0,2*Math.PI)
                if(!handle.proxy)ctx.fill()
                ctx.stroke()
            }
            }
            if(overlay.brushOutline.length>0){
                ctx.save();ctx.beginPath();ctx.rect(overlay.photoX,overlay.photoY,overlay.photoWidth,overlay.photoHeight);ctx.clip()
                ctx.strokeStyle=editor.localBrushErase?"#00cbea":"white";ctx.lineWidth=1.5
                ctx.beginPath()
                for(let i=0;i<overlay.brushOutline.length;++i){const p=overlay.brushOutline[i];const x=overlay.photoX+p.x*overlay.photoWidth,y=overlay.photoY+p.y*overlay.photoHeight;if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)}
                ctx.closePath();ctx.stroke();ctx.restore()
            }
            ctx.restore()
        }
    }
    Connections { target: editor; function onPreviewChanged() { if(gesture.adjusting&&!editor.localGestureActive){gesture.adjusting=false;gesture.proxyDrag=false}overlay.refreshBrush() } }
    MouseArea {
        id: gesture
        objectName: "localMaskGestureArea"
        anchors.fill: parent
        enabled: !editor.busy && !editor.editingLocked
        cursorShape: Qt.CrossCursor
        hoverEnabled: true
        onExited: {overlay.hoverX=-1;overlay.refreshBrush()}
        property bool adjusting: false
        property bool proxyDrag: false
        onPressed: function(mouse) {
            if(overlay.photoWidth<=0||overlay.photoHeight<=0){mouse.accepted=false;return}
            const inside=overlay.insidePhoto(mouse.x,mouse.y)
            if(editor.localState.shape==="color"||editor.localState.shape==="luminance"){
                if(!inside){mouse.accepted=false;return}
                const p=overlay.photoPoint(mouse.x,mouse.y,true)
                editor.pickLocalRange(p.x,p.y);return
            }
            let selected=-1, distance=14*14, proxy=false
            for(const handle of overlay.handles) {
                const dx=handle.x*width-mouse.x,dy=handle.y*height-mouse.y,d=dx*dx+dy*dy
                if(d<distance) { selected=handle.handle;distance=d;proxy=handle.proxy }
            }
            if(selected<0&&!inside){mouse.accepted=false;return}
            const p=overlay.photoPoint(mouse.x,mouse.y,!proxy)
            adjusting=editor.beginLocalGesture(selected,p.x,p.y,proxy)
            proxyDrag=adjusting&&proxy
            if(!adjusting) mouse.accepted=false
        }
        onPositionChanged: function(mouse) {
            const inside=overlay.insidePhoto(mouse.x,mouse.y)
            if(editor.localState.shape==="brush"&&!inside){
                if(adjusting)editor.endEdit()
                adjusting=false;proxyDrag=false;overlay.hoverX=-1;overlay.refreshBrush();return
            }
            if(inside){const p=overlay.photoPoint(mouse.x,mouse.y,true);overlay.hoverX=p.x;overlay.hoverY=p.y}
            else overlay.hoverX=-1
            overlay.refreshBrush()
            if(pressed&&adjusting){const p=overlay.photoPoint(mouse.x,mouse.y,!proxyDrag);editor.adjustLocalGesture(p.x,p.y)}
        }
        onReleased: function(mouse) {
            if(adjusting){
                if(editor.localState.shape!=="brush"||overlay.insidePhoto(mouse.x,mouse.y)){
                    const p=overlay.photoPoint(mouse.x,mouse.y,!proxyDrag)
                    editor.adjustLocalGesture(p.x,p.y)
                }
                editor.endEdit()
            }
            adjusting=false;proxyDrag=false
        }
        onCanceled: { if(adjusting)editor.endEdit();adjusting=false;proxyDrag=false }
    }
}
