import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1100
    height: 900
    minimumWidth: 480
    minimumHeight: 360
    visible: true
    title: backend.fileName ? backend.fileName + " - Omaread" : "Omaread"

    readonly property var colors: backend.colors
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) times the user's own text size set
    // with Ctrl+= / Ctrl+- / Ctrl+wheel.
    readonly property real textScale: backend.textScale
    readonly property int bodySize: scaled(16)
    readonly property int footerHeight: scaled(28)
    // github.com's reading column, with a margin either side.
    readonly property int columnWidth: Math.max(0, Math.min(scaled(880), width - 2 * scaled(32)))
    property string hoveredLink: ""
    property var selectionOwner: null
    property real savedY: 0

    Material.theme: backend.darkMode ? Material.Dark : Material.Light
    Material.accent: colors.link
    color: colors.background

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        width = geometry.width;
        height = geometry.height;
        if (geometry.maximized)
            showMaximized();
    }

    onClosing: backend.saveWindowGeometry(width, height, visibility === Window.Maximized)

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaled(pixels) {
        return Math.max(1, Math.round(pixels * textScale));
    }

    // Qt's rich text takes its link and code styling from a style sheet in the
    // text itself. Line height is a multiple of the text size, as in CSS; Qt's
    // own percentages scale the font's line spacing, which runs tall.
    function richText(html, size, lineHeight) {
        return "<style>a { color: " + colors.link + "; text-decoration: none; }"
            + " code { font-family: '" + backend.codeFont + "'; font-size: " + Math.round(size * 0.85) + "px;"
            + " background-color: " + colors.codeBackground + "; }"
            + " p { margin: 0; }</style>"
            + "<div style=\"line-height: " + Math.round(size * lineHeight) + "px;\">" + html + "</div>";
    }

    function calloutColor(kind) {
        return colors[kind] || colors.border;
    }

    function followLink(link) {
        backend.activateLink(link, flick.contentY);
    }

    // One block's text holds the selection at a time, as one page would.
    function takeSelection(edit) {
        if (selectionOwner && selectionOwner !== edit)
            selectionOwner.deselect();
        selectionOwner = edit;
    }

    function maxY() {
        return Math.max(0, flick.contentHeight - flick.height);
    }

    function scrollTo(y) {
        flick.cancelFlick();
        flick.contentY = Math.max(0, Math.min(y, maxY()));
    }

    function scrollToAnchor(anchor) {
        var index = backend.anchorIndex(anchor);
        var item = index >= 0 ? blocks.itemAt(index) : null;
        if (!item)
            return;
        scrollTo(page.y + item.y + item.gapTop - scaled(16));
    }

    function toggleFullScreen() {
        visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen;
    }

    Connections {
        target: backend

        function onAboutToReload() {
            win.savedY = flick.contentY;
        }

        function onDocumentLoaded(anchor, position) {
            win.selectionOwner = null;
            win.hoveredLink = "";
            // Wait for the blocks to be laid out before moving through them.
            if (anchor)
                Qt.callLater(win.scrollToAnchor, anchor);
            else if (position < 0)
                Qt.callLater(win.scrollTo, win.savedY);
            else
                Qt.callLater(win.scrollTo, position);
        }

        function onAnchorRequested(anchor) {
            win.scrollToAnchor(anchor);
        }

        function onStatusChanged() {
            statusTimer.restart();
        }
    }

    Dialogs.FileDialog {
        id: openDialog
        title: "Open Markdown"
        nameFilters: ["Markdown (*.md *.markdown *.mdown *.mkd *.mkdn *.mdwn)", "All files (*)"]
        onAccepted: backend.open(selectedFile)
    }

    Shortcut {
        sequences: [StandardKey.Open]
        onActivated: openDialog.open()
    }

    Shortcut {
        sequences: ["Ctrl+R", "F5"]
        onActivated: backend.reload()
    }

    Shortcut {
        sequence: "Ctrl+E"
        enabled: backend.canEdit && backend.filePath !== ""
        onActivated: backend.edit()
    }

    Shortcut {
        sequences: ["Alt+Left", "Backspace"]
        enabled: backend.canGoBack
        onActivated: backend.goBack()
    }

    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++", "Ctrl+Shift+="]
        onActivated: backend.increaseTextSize()
    }

    Shortcut {
        sequence: "Ctrl+-"
        onActivated: backend.decreaseTextSize()
    }

    Shortcut {
        sequence: "Ctrl+0"
        onActivated: backend.resetTextSize()
    }

    Shortcut {
        sequences: [StandardKey.Copy]
        onActivated: {
            if (win.selectionOwner && win.selectionOwner.selectedText)
                backend.copyText(win.selectionOwner.selectedText);
        }
    }

    // Super+F, as in Omawrite; Qt names the key Meta.
    Shortcut {
        sequences: ["Meta+F", "F11"]
        onActivated: win.toggleFullScreen()
    }

    Shortcut {
        sequences: [StandardKey.Close, "Ctrl+Q"]
        onActivated: win.close()
    }

    Flickable {
        id: flick
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: footer.top
        }
        contentWidth: width
        contentHeight: page.y + page.height + win.scaled(64)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        focus: true

        ScrollBar.vertical: ScrollBar { }

        Keys.onPressed: (event) => {
            var line = win.scaled(48);
            var screen = flick.height - win.scaled(64);
            var by = 0;
            if (event.key === Qt.Key_Down || event.key === Qt.Key_J)
                by = line;
            else if (event.key === Qt.Key_Up || event.key === Qt.Key_K)
                by = -line;
            else if (event.key === Qt.Key_PageDown
                     || (event.key === Qt.Key_Space && !(event.modifiers & Qt.ShiftModifier)))
                by = screen;
            else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_Space)
                by = -screen;
            else if (event.key === Qt.Key_Home || (event.key === Qt.Key_G && !(event.modifiers & Qt.ShiftModifier)))
                by = -flick.contentY;
            else if (event.key === Qt.Key_End || event.key === Qt.Key_G)
                by = win.maxY() - flick.contentY;
            else
                return;
            win.scrollTo(flick.contentY + by);
            event.accepted = true;
        }

        WheelHandler {
            acceptedModifiers: Qt.ControlModifier
            onWheel: (event) => {
                if (event.angleDelta.y > 0)
                    backend.increaseTextSize();
                else if (event.angleDelta.y < 0)
                    backend.decreaseTextSize();
            }
        }

        // A click on the page, away from any text, drops the selection.
        TapHandler {
            onTapped: {
                if (win.selectionOwner)
                    win.selectionOwner.deselect();
                win.selectionOwner = null;
            }
        }

        Column {
            id: page
            x: Math.round((flick.width - win.columnWidth) / 2)
            y: win.scaled(40)
            width: win.columnWidth

            Repeater {
                id: blocks
                model: backend.blocks
                delegate: BlockItem {
                    width: page.width
                }
            }
        }
    }

    Column {
        anchors.centerIn: flick
        spacing: win.scaled(10)
        visible: backend.blocks.length === 0

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: backend.error || (backend.filePath ? backend.fileName + " is empty" : "Nothing open")
            color: win.colors.foreground
            font.family: backend.textFont
            font.pixelSize: win.scaled(20)
        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Ctrl+O opens a Markdown file, or drop one here"
            color: win.colors.muted
            font.family: backend.textFont
            font.pixelSize: win.scaled(14)
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls)
                backend.open(drop.urls[0]);
        }
    }

    Rectangle {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: win.footerHeight
        color: win.colors.background

        // Where a link goes while the pointer is on it, as a browser shows;
        // otherwise the file being read.
        Label {
            anchors {
                left: parent.left
                leftMargin: win.scaled(14)
                right: status.left
                rightMargin: win.scaled(14)
                verticalCenter: parent.verticalCenter
            }
            text: win.hoveredLink || backend.filePath
            elide: Text.ElideMiddle
            color: win.colors.muted
            font.family: backend.codeFont
            font.pixelSize: win.scaled(12)
        }

        Label {
            id: status
            anchors {
                right: parent.right
                rightMargin: win.scaled(14)
                verticalCenter: parent.verticalCenter
            }
            text: backend.status
            opacity: statusTimer.running ? 1 : 0
            color: win.colors.muted
            font.family: backend.codeFont
            font.pixelSize: win.scaled(12)

            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }
        }

        Timer {
            id: statusTimer
            interval: 2500
        }
    }
}
