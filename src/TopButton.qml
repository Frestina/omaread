import QtQuick
import QtQuick.Controls
import QtQuick.Window

// A round button with an arrow up, which takes the page back to its top. It
// fades in once the top is a whole window out of sight.
Item {
    id: control
    objectName: "topButton"

    // Whether the page is far enough down for it.
    property bool wanted: false

    signal clicked()

    width: win.scaled(40)
    height: win.scaled(40)
    opacity: wanted ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: 150 }
    }

    ToolTip.visible: hitArea.containsMouse
    ToolTip.delay: 500
    ToolTip.text: "Back to top (Home)"

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: hitArea.containsMouse ? win.colors.codeBackground : win.colors.background
        border.width: 1
        border.color: win.colors.border
    }

    Canvas {
        id: arrow

        // Drawn at the physical size and scaled back down, as the footer's
        // icons are, so it stays sharp.
        readonly property real dpr: Screen.devicePixelRatio
        anchors.centerIn: parent
        width: win.scaled(16) * dpr
        height: win.scaled(16) * dpr
        scale: 1 / dpr
        onDprChanged: requestPaint()
        onWidthChanged: requestPaint()

        onPaint: {
            var context = getContext("2d");
            var unit = width / 16;
            context.setTransform(unit, 0, 0, unit, 0, 0);
            context.clearRect(0, 0, 16, 16);
            context.strokeStyle = hitArea.containsMouse ? win.colors.foreground : win.colors.muted;
            context.lineWidth = 1.6;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();
            context.moveTo(8, 13);
            context.lineTo(8, 3.5);
            context.moveTo(3.5, 8);
            context.lineTo(8, 3.5);
            context.lineTo(12.5, 8);
            context.stroke();
        }

        Connections {
            target: hitArea
            function onContainsMouseChanged() { arrow.requestPaint(); }
        }

        Connections {
            target: win
            function onColorsChanged() { arrow.requestPaint(); }
        }
    }

    MouseArea {
        id: hitArea
        anchors.fill: parent
        enabled: control.wanted
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
