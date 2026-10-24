import QtQuick
import QtQuick.Controls
Item {
    id: wheel
    required property string channel
    property real hue: editor.parameters["grading."+channel+".hue"]
    property real saturation: editor.parameters["grading."+channel+".saturation"]
    implicitWidth: 170; implicitHeight: 170
    Accessible.name: qsTranslate("Luma", "Color wheel")
    Canvas {
        id: disc; anchors.fill: parent
        onPaint: {
            const ctx=getContext("2d"), cx=width/2, cy=height/2, radius=Math.min(width,height)/2-6
            ctx.clearRect(0,0,width,height)
            for(let angle=0;angle<360;angle+=2) {
                const a=(angle-90)*Math.PI/180,b=(angle-88)*Math.PI/180
                const gradient=ctx.createRadialGradient(cx,cy,0,cx,cy,radius)
                gradient.addColorStop(0,"#b8b8b8");gradient.addColorStop(1,Qt.hsla(angle/360,1,.5,1))
                ctx.fillStyle=gradient;ctx.beginPath();ctx.moveTo(cx,cy);ctx.arc(cx,cy,radius,a,b);ctx.closePath();ctx.fill()
            }
            const theta=(wheel.hue-90)*Math.PI/180, r=radius*wheel.saturation/100
            ctx.beginPath();ctx.arc(cx+Math.cos(theta)*r,cy+Math.sin(theta)*r,5,0,2*Math.PI)
            ctx.strokeStyle="white";ctx.lineWidth=2;ctx.stroke()
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }
    onHueChanged: disc.requestPaint()
    onSaturationChanged: disc.requestPaint()
    MouseArea {
        anchors.fill: parent
        enabled: editor.hasPhoto && !editor.busy && editor.correctionStates.grading
        function apply(mouse) {
            const x=mouse.x-width/2,y=mouse.y-height/2,r=Math.max(1,Math.min(width,height)/2-6)
            const hue=(Math.atan2(y,x)*180/Math.PI+450)%360
            editor.setParameter("grading."+wheel.channel+".hue",hue)
            editor.setParameter("grading."+wheel.channel+".saturation",Math.min(100,Math.hypot(x,y)/r*100))
        }
        onPressed: function(mouse) { editor.beginEdit();apply(mouse) }
        onPositionChanged: function(mouse) { if(pressed) apply(mouse) }
        onReleased: editor.endEdit()
        onCanceled: editor.endEdit()
    }
}
