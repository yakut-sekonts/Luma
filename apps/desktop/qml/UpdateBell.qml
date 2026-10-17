import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: bell
    objectName: "updates.bell"
    implicitWidth: 40
    implicitHeight: 36
    Accessible.name: updates.availableVersion.length ? qsTranslate("Luma", "Luma update available ")
                                                       + updates.availableVersion :
                                                       qsTranslate("Luma", "Luma updates")
    ToolTip.visible: hovered
    ToolTip.text: Accessible.name
    onClicked: notifications.open()
    contentItem: Item {
        Canvas {
            anchors.centerIn: parent
            width: 22
            height: 24
            onPaint: {
                const c = getContext("2d");
                c.clearRect(0, 0, width, height);
                c.strokeStyle = "#e3e5e9";
                c.lineWidth = 1.6;
                c.beginPath();
                c.moveTo(3, 18);
                c.lineTo(5, 15);
                c.lineTo(5, 9);
                c.bezierCurveTo(5, 1, 17, 1, 17, 9);
                c.lineTo(17, 15);
                c.lineTo(19, 18);
                c.closePath();
                c.stroke();
                c.beginPath();
                c.arc(11, 20, 2, 0, Math.PI);
                c.stroke();
            }
        }
        Rectangle {
            visible: updates.availableVersion.length > 0
            width: 8
            height: 8
            radius: 4
            color: "#dfae77"
            anchors.right: parent.right
            anchors.top: parent.top
        }
    }
    Popup {
        id: notifications
        objectName: "updates.popup"
        focus: true
        x: bell.width - width
        y: bell.height + 8
        width: 340
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        ColumnLayout {
            width: parent.width
            spacing: 12
            Label {
                text: qsTranslate("Luma", "Updates")
                font.pixelSize: 17
                font.bold: true
            }
            Label {
                visible: updates.availableVersion.length > 0
                text: "Luma " + updates.availableVersion
                color: "#dfae77"
                font.pixelSize: 15
            }
            Label {
                text: qsTranslate("Luma", updates.message)
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Button {
                visible: updates.availableVersion.length > 0
                text: qsTranslate("Luma", "Install update…")
                enabled: !updates.restartPending
                Layout.fillWidth: true
                onClicked: {
                    notifications.close();
                    updates.install();
                }
            }
            Button {
                text: updates.checking ? qsTranslate("Luma", "Checking…") : qsTranslate("Luma", "Check for updates")
                enabled: updates.canCheck && !updates.checking
                Layout.fillWidth: true
                onClicked: updates.check()
            }
            CheckBox {
                text: qsTranslate("Luma", "Check automatically")
                checked: updates.automatic
                onClicked: updates.automatic = checked
            }
        }
    }
}
