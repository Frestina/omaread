import QtQuick
import QtQuick.Controls
import QtQuick.Window

// A small line icon in the footer, as Omawrite's: a folder to open a file, a
// list for the outline, a keyboard for the shortcuts. Drawn on a 16-pixel
// grid scaled to the text.
Item {
    id: control

    property string iconName
    property color iconColor: win.colors.muted
    property string tooltip
    // Drawn in the accent while what it shows is open.
    property bool active: false

    signal clicked()

    opacity: enabled ? 1 : 0.4
    width: win.scaled(16)
    height: win.scaled(16)

    ToolTip.visible: hitArea.containsMouse && tooltip.length > 0
    ToolTip.delay: 500
    ToolTip.text: tooltip

    Canvas {
        id: iconCanvas

        // Canvas rasterizes one surface pixel per logical pixel and gets
        // upscaled blurry on hidpi screens. Draw at the physical size and
        // scale the item back down so surface pixels match screen pixels.
        readonly property real dpr: Screen.devicePixelRatio
        width: control.width * dpr
        height: control.height * dpr
        transformOrigin: Item.TopLeft
        scale: 1 / dpr
        onDprChanged: requestPaint()
        onWidthChanged: requestPaint()

        onPaint: {
            var context = getContext("2d");
            var unit = dpr * control.width / 16;
            context.setTransform(unit, 0, 0, unit, 0, 0);
            context.clearRect(0, 0, 16, 16);
            context.strokeStyle = control.active ? win.colors.link
                : hitArea.containsMouse ? win.colors.foreground : control.iconColor;
            context.lineWidth = 1.4;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();
            if (control.iconName === "keyboard") {
                context.moveTo(1.5, 3.5);
                context.lineTo(14.5, 3.5);
                context.lineTo(14.5, 12.5);
                context.lineTo(1.5, 12.5);
                context.closePath();
                // Two rows of keys over a space bar.
                for (var x = 4; x <= 12; x += 2.67) {
                    context.moveTo(x, 6.3);
                    context.lineTo(x + 0.01, 6.3);
                }
                for (x = 5.33; x <= 11; x += 2.67) {
                    context.moveTo(x, 8.4);
                    context.lineTo(x + 0.01, 8.4);
                }
                context.moveTo(5.5, 10.5);
                context.lineTo(10.5, 10.5);
            } else if (control.iconName === "outline") {
                // GitHub's list: a bullet and a line for each heading.
                for (var y = 4; y <= 12; y += 4) {
                    context.moveTo(2.5, y);
                    context.lineTo(2.51, y);
                    context.moveTo(5.5, y);
                    context.lineTo(13.5, y);
                }
            } else {
                context.moveTo(2.5, 13);
                context.lineTo(2.5, 3.5);
                context.lineTo(6.5, 3.5);
                context.lineTo(8.5, 5.5);
                context.lineTo(13.5, 5.5);
                context.lineTo(13.5, 13);
                context.closePath();
            }
            context.stroke();
        }

        Connections {
            target: control
            function onIconColorChanged() { iconCanvas.requestPaint(); }
            function onIconNameChanged() { iconCanvas.requestPaint(); }
            function onActiveChanged() { iconCanvas.requestPaint(); }
        }

        Connections {
            target: hitArea
            function onContainsMouseChanged() { iconCanvas.requestPaint(); }
        }
    }

    MouseArea {
        id: hitArea
        anchors.centerIn: parent
        width: win.scaled(28)
        height: win.scaled(28)
        enabled: control.enabled
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
