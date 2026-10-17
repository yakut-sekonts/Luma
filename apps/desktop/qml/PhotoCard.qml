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
    property bool compact: false
    property int retryCount: 0
    onThumbnailURLChanged: retryCount = 0
    color: selected ? "#393027" : "#1b1e24"
    radius: 6
    border.width: selected || activePhoto ? 2 : 1
    border.color: selected ? "#dfae77" : activePhoto ? "#859ec1" : "#2a2f38"
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
    Label { anchors.centerIn: thumbnail; visible: thumbnail.status !== Image.Ready; text: thumbnail.status === Image.Error ? qsTranslate("Luma", "No preview") : "…"; color: "#68717f"; font.pixelSize: 11 }
    Column {
        id: caption
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10 }
        spacing: 3
        Label { objectName: (card.compact ? "filmstrip.filename." : "library.filename.") + card.assetID; width: parent.width; text: card.filename || "…"; color: "#dce0e8"; elide: Text.ElideMiddle; font.pixelSize: card.compact ? 10 : 12 }
        Label { visible: !card.compact; text: (card.rating ? "★".repeat(card.rating) : "☆☆☆☆☆") + (card.flagValue === 1 ? "  Pick" : card.flagValue === -1 ? "  Reject" : ""); color: card.rating ? "#dfae77" : "#626c7b"; font.pixelSize: 12 }
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
            card.forceActiveFocus(Qt.MouseFocusReason)
            library.select(card.index, (mouse.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0, (mouse.modifiers & Qt.ShiftModifier) !== 0)
            if (card.compact && mouse.modifiers === Qt.NoModifier && !editor.busy) library.activate(card.index)
        }
        onDoubleClicked: { if (!editor.busy) library.activate(card.index) }
    }
}
