import QtQuick

// Selectable rich text whose links Omaread follows itself.
TextEdit {
    id: edit

    property string html
    property int size: win.bodySize
    property real lineHeight: 1.5
    // Searched by find in page.
    readonly property bool findable: true
    // The first line's baseline. A fixed line height puts some of its extra
    // space above the text, which baselineOffset leaves out.
    readonly property real firstBaseline: {
        contentHeight;
        return positionToRectangle(0).y + baselineOffset;
    }

    readOnly: true
    selectByMouse: true
    // Focus stays with the page, so the keys keep scrolling it; the selection
    // shows without focus.
    activeFocusOnPress: false
    persistentSelection: true
    wrapMode: TextEdit.Wrap
    textFormat: TextEdit.RichText
    font.family: backend.textFont
    font.pixelSize: size
    color: win.colors.foreground
    selectionColor: win.colors.selection
    selectedTextColor: win.colors.selectionForeground
    text: win.richText(html, size, lineHeight)

    onLinkActivated: (link) => win.followLink(link)
    onLinkHovered: (link) => win.hoveredLink = link
    onSelectedTextChanged: {
        if (selectedText)
            win.takeSelection(edit);
    }

    HoverHandler {
        cursorShape: edit.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
    }
}
