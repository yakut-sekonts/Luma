import QtQuick

Item {
    id: overlay
    property rect paintBounds: Qt.rect(0,0,width,height)
    objectName: "cropOverlay"
    visible: editor.cropMode && editor.cropCanvasReady && editor.hasPhoto && !editor.busy && !editor.original && editor.correctionStates.crop
    property var frame: editor.cropState
    property string guide: editor.cropOverlay
    property string dragMode: ""
    property var startFrame: ({})
    property real startX: 0
    property real startY: 0
    onFrameChanged: drawing.requestPaint()
    onGuideChanged: drawing.requestPaint()
    onWidthChanged: drawing.requestPaint()
    onHeightChanged: drawing.requestPaint()
    function bound(value, low, high) { return Math.max(low, Math.min(high, value)) }
    function beginDrag(px, py) {
        if (!visible || width<=0 || height<=0) return false
        startX=bound(px/width,0,1);startY=bound(py/height,0,1)
        startFrame={x:frame.x,y:frame.y,width:frame.width,height:frame.height}
        const l=frame.x, t=frame.y, r=l+frame.width, b=t+frame.height
        const nearX=10/width, nearY=10/height
        const inX=startX>=l-nearX&&startX<=r+nearX, inY=startY>=t-nearY&&startY<=b+nearY
        let mode=""
        if(inY&&Math.abs(startX-l)<=nearX)mode+="l"
        else if(inY&&Math.abs(startX-r)<=nearX)mode+="r"
        if(inX&&Math.abs(startY-t)<=nearY)mode+="t"
        else if(inX&&Math.abs(startY-b)<=nearY)mode+="b"
        dragMode=mode.length?mode:(startX>=l&&startX<=r&&startY>=t&&startY<=b?"move":"new")
        editor.beginEdit()
        return true
    }
    function updateDrag(px, py) {
        if(!dragMode.length || width<=0 || height<=0)return
        const nx=bound(px/width,0,1),ny=bound(py/height,0,1),dx=nx-startX,dy=ny-startY
        const f=startFrame,minimum=1/20000
        if(dragMode==="move") {editor.setCropRect(bound(f.x+dx,0,1-f.width),bound(f.y+dy,0,1-f.height),f.width,f.height);return}
        let l=f.x,t=f.y,r=f.x+f.width,b=f.y+f.height
        let mode=dragMode
        if(mode==="new") {l=Math.min(nx,startX);r=Math.max(nx,startX);t=Math.min(ny,startY);b=Math.max(ny,startY);mode=(nx>=startX?"r":"l")+(ny>=startY?"b":"t")}
        else {
            if(mode.indexOf("l")>=0)l=bound(f.x+dx,0,r-minimum)
            if(mode.indexOf("r")>=0)r=bound(r+dx,l+minimum,1)
            if(mode.indexOf("t")>=0)t=bound(f.y+dy,0,b-minimum)
            if(mode.indexOf("b")>=0)b=bound(b+dy,t+minimum,1)
        }
        const ratio=frame.normalizedRatio??0
        if(ratio>0) {
            const horizontal=mode.indexOf("l")>=0||mode.indexOf("r")>=0
            const vertical=mode.indexOf("t")>=0||mode.indexOf("b")>=0
            const ax=mode.indexOf("l")>=0?(dragMode==="new"?startX:f.x+f.width):(dragMode==="new"?startX:f.x)
            const ay=mode.indexOf("t")>=0?(dragMode==="new"?startY:f.y+f.height):(dragMode==="new"?startY:f.y)
            let w=r-l,h=b-t
            if(horizontal&&vertical) {
                if(Math.abs(dx)*width>=Math.abs(dy)*height)h=w/ratio;else w=h*ratio
                const maxW=mode.indexOf("l")>=0?ax:1-ax,maxH=mode.indexOf("t")>=0?ay:1-ay
                w=Math.min(w,maxW,maxH*ratio);h=w/ratio
                l=mode.indexOf("l")>=0?ax-w:ax;t=mode.indexOf("t")>=0?ay-h:ay
            } else if(horizontal) {
                const cy=f.y+f.height/2,maxW=mode.indexOf("l")>=0?ax:1-ax
                w=Math.min(w,maxW,2*Math.min(cy,1-cy)*ratio);h=w/ratio
                l=mode.indexOf("l")>=0?ax-w:ax;t=cy-h/2
            } else {
                const cx=f.x+f.width/2,maxH=mode.indexOf("t")>=0?ay:1-ay
                h=Math.min(h,maxH,2*Math.min(cx,1-cx)/ratio);w=h*ratio
                l=cx-w/2;t=mode.indexOf("t")>=0?ay-h:ay
            }
            if(w<minimum||h<minimum)return
            r=l+w;b=t+h
        }
        if(r-l<minimum||b-t<minimum)return
        editor.setCropRect(bound(l,0,1),bound(t,0,1),r-l,b-t)
    }
    function endDrag() { if(dragMode.length) {dragMode="";editor.endEdit()} }
    onVisibleChanged: {if(!visible)endDrag()}
    Canvas {
        id: drawing
        objectName: "CropOverlay.canvas"
        x: overlay.paintBounds.x; y: overlay.paintBounds.y
        width: Math.max(1, overlay.paintBounds.width); height: Math.max(1, overlay.paintBounds.height)
        onXChanged: requestPaint()
        onYChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c=getContext("2d");c.clearRect(0,0,drawing.width,drawing.height)
            const width=overlay.width, height=overlay.height
            c.save();c.translate(-drawing.x,-drawing.y)
            const f=overlay.frame
            if(f.x===undefined){c.restore();return}
            const l=f.x*width,t=f.y*height,w=f.width*width,h=f.height*height
            c.fillStyle="rgba(0,0,0,0.55)"
            c.fillRect(0,0,width,t);c.fillRect(0,t+h,width,height-t-h);c.fillRect(0,t,l,h);c.fillRect(l+w,t,width-l-w,h)
            c.strokeStyle="rgba(255,255,255,0.75)";c.lineWidth=1;c.strokeRect(l,t,w,h)
            function line(x1,y1,x2,y2){c.beginPath();c.moveTo(l+x1*w,t+y1*h);c.lineTo(l+x2*w,t+y2*h);c.stroke()}
            if(overlay.guide==="diagonal"){line(0,0,1,1);line(0,1,1,0)}
            else {
                const positions=overlay.guide==="grid"?[0.2,0.4,0.6,0.8]:overlay.guide==="golden"?[0.381966,0.618034]:[1/3,2/3]
                for(let i=0;i<positions.length;i++){const p=positions[i];line(p,0,p,1);line(0,p,1,p)}
            }
            c.fillStyle="#dfae77"
            const grips=[[0,0],[0.5,0],[1,0],[0,0.5],[1,0.5],[0,1],[0.5,1],[1,1]]
            for(let i=0;i<grips.length;i++){c.beginPath();c.arc(l+grips[i][0]*w,t+grips[i][1]*h,4,0,Math.PI*2);c.fill()}
            c.restore()
        }
    }
    MouseArea {
        objectName: "cropDragArea"
        anchors.fill: parent
        preventStealing: true
        cursorShape: overlay.dragMode==="move"?Qt.ClosedHandCursor:Qt.CrossCursor
        onPressed: function(mouse){mouse.accepted=overlay.beginDrag(mouse.x,mouse.y)}
        onPositionChanged: function(mouse){if(pressed)overlay.updateDrag(mouse.x,mouse.y)}
        onReleased: overlay.endDrag()
        onCanceled: overlay.endDrag()
    }
}
