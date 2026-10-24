import QtQuick
import QtQuick.Controls

Rectangle {
    id: card
    objectName: (compact ? "filmstrip.card." : "library.card.") + assetID
    required property int index
    required property string assetID
    required property string filename
    required property string thumbnailURL
    required property int rating
    required property int flagValue
    required property string colorLabel
    required property bool selected
    required property bool activePhoto
    required property bool sourceMissing
    property bool compact: false
    property int retryCount: 0
    readonly property var keyboardView: compact ? card.ListView.view : card.GridView.view
    readonly property bool keyboardFocusEligible: assetID.length > 0 &&
        keyboardView && keyboardView.visible && parent === keyboardView.contentItem &&
        width > 0 && height > 0 &&
        x >= keyboardView.contentX && x + width <= keyboardView.contentX + keyboardView.width &&
        // A short Library viewport can be lower than one card. Its visible
        // center remains a usable keyboard target even when the caption is clipped.
        y + height / 2 >= keyboardView.contentY &&
        y + height / 2 < keyboardView.contentY + keyboardView.height
    // Qt forbids clearing the Tab flag on its active focus item.
    activeFocusOnTab: keyboardFocusEligible || activeFocus
    function openFromKeyboard(event) {
        event.accepted = event.modifiers === Qt.NoModifier ||
            (event.key === Qt.Key_Enter && event.modifiers === Qt.KeypadModifier)
        if (!event.accepted || event.isAutoRepeat || editor.busy || !card.activeFocus ||
            !card.keyboardFocusEligible)
            return
        const row = card.index
        library.select(row, false, false)
        library.activate(row)
    }
    Keys.onReturnPressed: function(event) {
        card.openFromKeyboard(event)
    }
    Keys.onEnterPressed: function(event) {
        card.openFromKeyboard(event)
    }
    onThumbnailURLChanged: retryCount = 0
    color: selected ? "#393027" : "#1b1e24"
    radius: 6
    border.width: selected || activePhoto || activeFocus ? 2 : 1
    border.color: selected ? "#dfae77" : activePhoto || activeFocus ? "#859ec1" : "#2a2f38"
    Image {
        id: thumbnail
        objectName: "photoThumbnail"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: caption.top; margins: 10 }
        source: card.thumbnailURL + (card.retryCount ? (card.thumbnailURL.indexOf("?") >= 0 ? "&" : "?") + "retry=" + card.retryCount : "")
        sourceSize: Qt.size(384,384)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        cache: true
    }
    Timer {
        objectName: "thumbnailRetryTimer"
        interval: 1500; running: thumbnail.status === Image.Error && card.retryCount < 2; repeat: false
        onTriggered: card.retryCount++
    }
    Label { objectName: (card.compact ? "filmstrip.availability." : "library.availability.") + card.assetID; anchors.centerIn: thumbnail; visible: card.sourceMissing || thumbnail.status !== Image.Ready; text: card.sourceMissing ? qsTranslate("Luma", "Missing") : thumbnail.status === Image.Error ? qsTranslate("Luma", "No preview") : "…"; color: "#68717f"; font.pixelSize: 11 }
    Column {
        id: caption
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
        spacing: 3
        Label { objectName: (card.compact ? "filmstrip.filename." : "library.filename.") + card.assetID; width: parent.width; text: card.filename || "…"; color: "#dce0e8"; elide: Text.ElideMiddle; font.pixelSize: card.compact ? 10 : 12 }
        Label { objectName: (card.compact ? "filmstrip.flagStatus." : "library.flagStatus.") + card.assetID; visible: !card.compact; text: (card.rating ? "★".repeat(card.rating) : "☆☆☆☆☆") + (card.flagValue === 1 ? "  " + qsTranslate("Luma", "Pick") : card.flagValue === -1 ? "  " + qsTranslate("Luma", "Reject") : ""); color: card.rating ? "#dfae77" : "#626c7b"; font.pixelSize: 12 }
    }
    Rectangle {
        visible: card.colorLabel.length > 0
        anchors { right: parent.right; top: parent.top; margins: 7 }
        width: 8; height: 8; radius: 4
        color: ({red:"#d56868",yellow:"#d9be67",green:"#78a985",blue:"#789bc8",purple:"#af83b8"})[card.colorLabel] || "transparent"
    }
    MouseArea {
        anchors.fill: parent
        onClicked: function(mouse) {
            // Set the actual clicked item before activating the view's focus
            // scope; otherwise it may reveal its stale current item (row zero).
            const view = card.keyboardView
            const previousX = view ? view.contentX : 0
            const previousY = view ? view.contentY : 0
            if (view) view.currentIndex = card.index
            card.forceActiveFocus(Qt.MouseFocusReason)
            // Mouse selection must retain the viewport, including a partly
            // visible row. Explicit positioning also cancels Qt's deferred
            // current-cell reveal; keyboard navigation still follows its item.
            if (view) {
                if (card.compact) view.contentX = previousX
                else view.contentY = previousY
            }
            library.select(card.index, (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0, (mouse.modifiers & Qt.ShiftModifier) !== 0)
            if (card.compact && mouse.modifiers === Qt.NoModifier && !editor.busy) library.activate(card.index)
        }
        onDoubleClicked: { if (!editor.busy) library.activate(card.index) }
    }
}
