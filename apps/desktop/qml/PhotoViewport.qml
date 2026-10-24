import QtQuick
import QtQuick.Controls

Flickable {
    id: viewport
    required property var controller
    readonly property real pixelRatio: Screen.devicePixelRatio
    readonly property size sourceSize: controller.previewPixelSize
    readonly property string zoomMode: controller.zoomMode
    readonly property bool pixelMode: zoomMode !== "fit"
    readonly property real sourceWidth: Math.max(1, sourceSize.width / pixelRatio)
    readonly property real sourceHeight: Math.max(1, sourceSize.height / pixelRatio)
    readonly property real fitZoom: Math.min(width / sourceWidth, height / sourceHeight)
    readonly property real fillZoom: Math.max(width / sourceWidth, height / sourceHeight)
    readonly property real effectiveZoom: zoomMode === "fit" ? fitZoom : zoomMode === "fill" ? fillZoom : controller.zoomFactor
    readonly property real imageItemWidth: pixelMode ? sourceWidth * effectiveZoom : width
    readonly property real imageItemHeight: pixelMode ? sourceHeight * effectiveZoom : height

    readonly property bool toolActive: controller.cropMode || controller.guidedMode || controller.localMode || controller.targetedTool !== "none"
    property bool pendingPlacement: false
    property point pendingSource: Qt.point(0.5, 0.5)
    property point pendingPoint: Qt.point(0, 0)
    property real pendingZoom: 1

    clip: true
    interactive: pixelMode && controller.zoomAvailable && !toolActive
    contentWidth: Math.max(width, imageItemWidth)
    contentHeight: Math.max(height, imageItemHeight)
    boundsBehavior: Flickable.StopAtBounds

    // Overlay items retain source-space geometry; only their Canvas backing
    // stores cover the visible viewport, even at the maximum zoom factor.
    function visibleBounds(itemX, itemY, itemWidth, itemHeight) {
        const left = Math.max(0, contentX - itemX)
        const top = Math.max(0, contentY - itemY)
        return Qt.rect(left, top, Math.max(0, Math.min(itemWidth, contentX + width - itemX) - left),
                       Math.max(0, Math.min(itemHeight, contentY + height - itemY) - top))
    }
    function bounded(value, maximum) {
        return Math.max(0, Math.min(Math.max(0, maximum), value))
    }
    function offsetFor(source, point, factor) {
        const imageWidth = sourceWidth * factor
        const imageHeight = sourceHeight * factor
        const canvasWidth = Math.max(width, imageWidth)
        const canvasHeight = Math.max(height, imageHeight)
        return Qt.point(bounded((canvasWidth - imageWidth) / 2 + source.x * imageWidth - point.x, canvasWidth - width),
                        bounded((canvasHeight - imageHeight) / 2 + source.y * imageHeight - point.y, canvasHeight - height))
    }
    function sourceAt(point) {
        // Consecutive wheel events can arrive before Flickable's new size has
        // settled. Use the pending transform, including boundary clamping.
        const factor = pendingPlacement ? pendingZoom : effectiveZoom
        const offset = pendingPlacement ? offsetFor(pendingSource, pendingPoint, factor) : Qt.point(contentX, contentY)
        const imageWidth = sourceWidth * factor
        const imageHeight = sourceHeight * factor
        return Qt.point(bounded((offset.x + point.x - (Math.max(width, imageWidth) - imageWidth) / 2) / imageWidth, 1),
                        bounded((offset.y + point.y - (Math.max(height, imageHeight) - imageHeight) / 2) / imageHeight, 1))
    }
    function applyPlacement() {
        cancelFlick()
        if (!pixelMode) {
            contentX = 0
            contentY = 0
        } else {
            const point = pendingPlacement ? pendingPoint : Qt.point(width / 2, height / 2)
            const source = pendingPlacement ? pendingSource : Qt.point(0.5, 0.5)
            const offset = offsetFor(source, point, effectiveZoom)
            // Preserve exact backing-pixel alignment at 1:1.
            contentX = effectiveZoom === 1 ? Math.round(offset.x * pixelRatio) / pixelRatio : offset.x
            contentY = effectiveZoom === 1 ? Math.round(offset.y * pixelRatio) / pixelRatio : offset.y
        }
        pendingPlacement = false
    }
    function schedulePlacement() {
        Qt.callLater(applyPlacement)
    }
    function requestScale(factor, point) {
        if (!controller.zoomAvailable || !Number.isFinite(factor) || factor <= 0) return
        const next = Math.max(controller.minimumZoomFactor, Math.min(controller.maximumZoomFactor, factor))
        const anchor = point === undefined ? Qt.point(width / 2, height / 2) : point
        const source = sourceAt(anchor)
        pendingSource = source
        pendingPoint = anchor
        pendingZoom = next
        pendingPlacement = true
        controller.zoomFactor = next
        if (controller.zoomMode !== "manual" || controller.zoomFactor !== next) {
            pendingPlacement = false
            return
        }
        schedulePlacement()
    }
    function panBy(dx, dy) {
        if (!pixelMode || !controller.zoomAvailable) return
        if (pendingPlacement) applyPlacement()
        cancelFlick()
        contentX = bounded(contentX + dx, contentWidth - width)
        contentY = bounded(contentY + dy, contentHeight - height)
    }
    onSourceSizeChanged: schedulePlacement()
    onZoomModeChanged: schedulePlacement()
    onEffectiveZoomChanged: schedulePlacement()
    onWidthChanged: schedulePlacement()
    onHeightChanged: schedulePlacement()
    Connections {
        target: viewport.controller
        function onPhotoLoaded() {
            viewport.pendingPlacement = false
            viewport.schedulePlacement()
        }
    }
    ScrollBar.horizontal: ScrollBar { policy: viewport.pixelMode ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
    ScrollBar.vertical: ScrollBar { policy: viewport.pixelMode ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
    WheelHandler {
        id: zoomWheel
        parent: viewport
        target: null
        enabled: viewport.controller.zoomAvailable
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            // macOS smooth/trackpad scrolling pans, including OS momentum.
            // Discrete vertical mouse-wheel steps zoom around the pointer.
            if (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0)
                viewport.panBy(-event.pixelDelta.x, -event.pixelDelta.y)
            else if (event.angleDelta.y !== 0)
                viewport.requestScale(viewport.effectiveZoom * Math.pow(2, event.angleDelta.y / 360), viewport.mapFromItem(null, zoomWheel.point.scenePosition))
            else if (event.angleDelta.x !== 0)
                viewport.panBy(-event.angleDelta.x / 4, 0)
            event.accepted = true
        }
    }
    PinchHandler {
        parent: viewport
        target: null
        enabled: viewport.controller.zoomAvailable
        rotationAxis.enabled: false
        xAxis.enabled: false
        yAxis.enabled: false
        onScaleChanged: delta => {
            if (delta > 0 && delta !== 1)
                viewport.requestScale(viewport.effectiveZoom * delta, viewport.mapFromItem(null, centroid.scenePosition))
        }
    }
    TapHandler {
        parent: viewport
        enabled: viewport.controller.zoomAvailable && !viewport.toolActive
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: (point, button) => {
            if (viewport.controller.oneToOne)
                viewport.controller.zoomMode = "fit"
            else
                viewport.requestScale(1, viewport.mapFromItem(null, point.scenePosition))
        }
    }
}
