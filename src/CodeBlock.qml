import QtQuick
import QtQuick.Controls

// A fenced code block on GitHub's rounded panel. Long lines scroll sideways
// rather than wrap, and a Copy button shows while the pointer is over it.
Rectangle {
    id: panel

    required property var block
    readonly property int padding: win.scaled(16)

    implicitHeight: code.implicitHeight + 2 * padding
        + (scroller.contentWidth > scroller.width ? win.scaled(8) : 0)
    radius: win.scaled(6)
    color: win.colors.codeBackground

    HoverHandler {
        id: hover
    }

    Flickable {
        id: scroller
        anchors {
            fill: parent
            leftMargin: panel.padding
            rightMargin: panel.padding
            topMargin: panel.padding
        }
        contentWidth: code.implicitWidth
        contentHeight: code.implicitHeight
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width
        clip: true

        ScrollBar.horizontal: ScrollBar {
            policy: scroller.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        TextEdit {
            id: code
            readonly property bool findable: true
            // Highlighted code is rich text, coloured by the palette's style
            // sheet for its spans; a language it does not know stays plain.
            text: panel.block.html
                ? "<style>" + win.colors.syntax + "</style><div style=\"white-space: pre;\">" + panel.block.html + "</div>"
                : panel.block.code
            textFormat: panel.block.html ? TextEdit.RichText : TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            activeFocusOnPress: false
            persistentSelection: true
            font.family: backend.codeFont
            font.pixelSize: Math.round(win.bodySize * 0.85)
            color: win.colors.foreground
            selectionColor: win.colors.selection
            selectedTextColor: win.colors.selectionForeground
            onSelectedTextChanged: {
                if (selectedText)
                    win.takeSelection(code);
            }

            HoverHandler {
                cursorShape: Qt.IBeamCursor
            }
        }
    }

    Rectangle {
        anchors {
            top: parent.top
            right: parent.right
            margins: win.scaled(8)
        }
        visible: hover.hovered
        width: copyLabel.implicitWidth + win.scaled(16)
        height: copyLabel.implicitHeight + win.scaled(8)
        radius: win.scaled(6)
        color: copyHover.hovered ? win.colors.border : win.colors.background
        border.width: 1
        border.color: win.colors.border

        Text {
            id: copyLabel
            anchors.centerIn: parent
            text: "Copy"
            color: win.colors.muted
            font.family: backend.textFont
            font.pixelSize: win.scaled(12)
        }

        HoverHandler {
            id: copyHover
            cursorShape: Qt.PointingHandCursor
        }

        TapHandler {
            onTapped: backend.copyText(panel.block.code)
        }
    }
}
