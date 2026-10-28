import QtQuick

// The mark beside a GitHub alert's title: an info circle for a note, a bulb
// for a tip, a speech bubble for important, a triangle for a warning and an
// octagon for caution.
Canvas {
    id: icon

    property string kind: "note"
    property color color: "black"

    onKindChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d");
        var s = width / 16;
        ctx.reset();
        ctx.strokeStyle = icon.color;
        ctx.fillStyle = icon.color;
        ctx.lineWidth = 1.5 * s;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";

        function exclamation(top, bottom, dot) {
            ctx.beginPath();
            ctx.moveTo(8 * s, top * s);
            ctx.lineTo(8 * s, bottom * s);
            ctx.stroke();
            ctx.beginPath();
            ctx.arc(8 * s, dot * s, 1 * s, 0, 2 * Math.PI);
            ctx.fill();
        }

        if (kind === "note") {
            ctx.beginPath();
            ctx.arc(8 * s, 8 * s, 6.75 * s, 0, 2 * Math.PI);
            ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(8 * s, 7.25 * s);
            ctx.lineTo(8 * s, 11.5 * s);
            ctx.stroke();
            ctx.beginPath();
            ctx.arc(8 * s, 4.75 * s, 1 * s, 0, 2 * Math.PI);
            ctx.fill();
        } else if (kind === "tip") {
            ctx.beginPath();
            ctx.arc(8 * s, 6.25 * s, 4.75 * s, Math.PI * 0.8, Math.PI * 2.2);
            ctx.lineTo(10 * s, 12 * s);
            ctx.lineTo(6 * s, 12 * s);
            ctx.closePath();
            ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(6.5 * s, 14.5 * s);
            ctx.lineTo(9.5 * s, 14.5 * s);
            ctx.stroke();
        } else if (kind === "important") {
            ctx.beginPath();
            ctx.moveTo(2.5 * s, 1.75 * s);
            ctx.lineTo(13.5 * s, 1.75 * s);
            ctx.quadraticCurveTo(14.75 * s, 1.75 * s, 14.75 * s, 3 * s);
            ctx.lineTo(14.75 * s, 10.5 * s);
            ctx.quadraticCurveTo(14.75 * s, 11.75 * s, 13.5 * s, 11.75 * s);
            ctx.lineTo(7 * s, 11.75 * s);
            ctx.lineTo(4 * s, 14.5 * s);
            ctx.lineTo(4 * s, 11.75 * s);
            ctx.lineTo(2.5 * s, 11.75 * s);
            ctx.quadraticCurveTo(1.25 * s, 11.75 * s, 1.25 * s, 10.5 * s);
            ctx.lineTo(1.25 * s, 3 * s);
            ctx.quadraticCurveTo(1.25 * s, 1.75 * s, 2.5 * s, 1.75 * s);
            ctx.stroke();
            exclamation(4.25, 7, 9.25);
        } else if (kind === "warning") {
            ctx.beginPath();
            ctx.moveTo(8 * s, 1.5 * s);
            ctx.lineTo(15 * s, 14.25 * s);
            ctx.lineTo(1 * s, 14.25 * s);
            ctx.closePath();
            ctx.stroke();
            exclamation(6, 9.5, 11.75);
        } else {
            ctx.beginPath();
            for (var i = 0; i < 8; ++i) {
                var angle = Math.PI / 8 + i * Math.PI / 4;
                var x = 8 * s + 6.9 * s * Math.cos(angle);
                var y = 8 * s + 6.9 * s * Math.sin(angle);
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.stroke();
            exclamation(4.5, 8.5, 11);
        }
    }
}
