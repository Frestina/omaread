import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
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
    // The <details> opened or closed by hand, by id; the rest are as the file
    // has them.
    property var detailsState: ({})
    property bool findOpen: false
    // Every match on the page in reading order, as {edit, start}.
    property var findMatches: []
    property int findIndex: -1
    // The find bar lies over the top of the page, so the page makes room
    // for it there.
    readonly property int findRoom: findOpen ? findBar.height + 12 : 0

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
        // Formulas are measured for 16px text, and grow with it.
        html = html.replace(/(<img class="math"[^>]*?) width="([\d.]+)" height="([\d.]+)"/g,
                            (match, start, width, height) => start + " width=\"" + width * size / 16
                                + "\" height=\"" + height * size / 16 + "\"");
        return "<style>a { color: " + colors.link + "; text-decoration: none; }"
            + " code { font-family: '" + backend.codeFont + "'; font-size: " + Math.round(size * 0.85) + "px;"
            + " background-color: " + colors.codeBackground + "; }"
            + " p { margin: 0; }</style>"
            + "<div style=\"line-height: " + Math.round(size * lineHeight) + "px;\">" + html + "</div>";
    }

    function detailsOpen(id, initial) {
        var state = detailsState[id];
        return state === undefined ? initial : state;
    }

    // Opens every <details> a block is folded away in.
    function unfold(block) {
        var state = Object.assign({}, detailsState);
        for (var i = 0; i < block.containers.length; ++i) {
            if (block.containers[i].kind === "details")
                state[block.containers[i].id] = true;
        }
        detailsState = state;
    }

    function toggleDetails(id, initial) {
        var state = Object.assign({}, detailsState);
        state[id] = !detailsOpen(id, initial);
        detailsState = state;
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
        // A heading inside a closed <details> opens it, then is laid out.
        if (item.folded) {
            unfold(item.block);
            Qt.callLater(scrollToAnchor, anchor);
            return;
        }
        scrollTo(page.y + item.y + item.gapTop - scaled(16));
    }

    // The texts find in page searches, in reading order.
    function findableTexts(item, out) {
        for (var i = 0; i < item.children.length; ++i) {
            var child = item.children[i];
            if (child.findable === true)
                out.push(child);
            else
                findableTexts(child, out);
        }
        return out;
    }

    // Each text marks its own matches. A new search starts from the first
    // match on screen or below it, as a browser's does; one run again, after
    // a reload, keeps its place.
    function updateFind(fromScreen) {
        findDelay.stop();
        var query = findOpen ? findField.text : "";
        var previous = findIndex;
        var matches = [];
        var texts = findableTexts(page, []);
        for (var i = 0; i < texts.length; ++i) {
            var starts = backend.find(texts[i].textDocument, query, -1);
            for (var j = 0; j < starts.length; ++j)
                matches.push({ edit: texts[i], start: starts[j] });
        }
        findMatches = matches;
        if (matches.length === 0)
            findIndex = -1;
        else if (fromScreen)
            findIndex = firstMatchOnScreen();
        else
            findIndex = Math.max(0, Math.min(previous, matches.length - 1));
        showMatch();
    }

    function matchY(match) {
        var rect = match.edit.positionToRectangle(match.start);
        return match.edit.mapToItem(page, rect.x, rect.y).y + page.y;
    }

    function firstMatchOnScreen() {
        for (var i = 0; i < findMatches.length; ++i) {
            if (!blockOf(findMatches[i].edit).folded && matchY(findMatches[i]) >= flick.contentY)
                return i;
        }
        return 0;
    }

    function blockOf(item) {
        while (item && item.folded === undefined)
            item = item.parent;
        return item;
    }

    function moveFind(direction) {
        // A search still waiting for the typing to pause lands on its first
        // match, which is where the reader meant to go.
        if (findDelay.running) {
            updateFind(true);
            return;
        }
        if (findMatches.length === 0)
            return;
        var previous = findMatches[findIndex];
        findIndex = (findIndex + direction + findMatches.length) % findMatches.length;
        if (previous && previous.edit !== findMatches[findIndex].edit)
            backend.find(previous.edit.textDocument, findField.text, -1);
        showMatch();
    }

    // Marks the current match and scrolls to it, opening any <details> it
    // is folded in first.
    function showMatch() {
        var match = findMatches[findIndex];
        if (!match)
            return;
        backend.find(match.edit.textDocument, findField.text, match.start);
        var block = blockOf(match.edit);
        if (block && block.folded) {
            unfold(block.block);
            Qt.callLater(revealMatch, match);
        } else {
            revealMatch(match);
        }
    }

    function revealMatch(match) {
        var rect = match.edit.positionToRectangle(match.start);
        // A code panel or table scrolls sideways to it, then the page down.
        for (var item = match.edit.parent; item && item !== flick; item = item.parent) {
            if (item.contentX === undefined || !item.interactive)
                continue;
            var x = match.edit.mapToItem(item.contentItem, rect.x, 0).x;
            if (x < item.contentX || x + rect.width > item.contentX + item.width)
                item.contentX = Math.max(0, Math.min(x - item.width / 3, item.contentWidth - item.width));
        }
        var y = matchY(match);
        var top = flick.contentY + findBar.height + scaled(24);
        if (y < top || y + rect.height > flick.contentY + flick.height - scaled(24))
            scrollTo(y - flick.height / 3);
    }

    // The page keeps still on screen as the room for the bar comes and goes.
    function openFind() {
        if (!findOpen && flick.contentY > 0) {
            findOpen = true;
            flick.contentY += findRoom;
        }
        findOpen = true;
        findField.forceActiveFocus();
        findField.selectAll();
        updateFind(true);
    }

    function closeFind() {
        findDelay.stop();
        // The page takes the keys again, to scroll.
        flick.forceActiveFocus();
        if (findOpen && flick.contentY > 0)
            flick.contentY = Math.max(0, flick.contentY - findRoom);
        findOpen = false;
        updateFind(false);
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
            // A reload keeps what was opened; another file starts afresh.
            if (position >= 0)
                win.detailsState = {};
            win.findMatches = [];
            if (win.findOpen)
                Qt.callLater(win.updateFind, position >= 0);
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

        // The marks take the new palette's colours.
        function onColorsChanged() {
            if (win.findOpen)
                Qt.callLater(win.updateFind, false);
        }
    }

    // Marking every text again for each letter typed would stall a long
    // page, so the search waits until the typing pauses.
    Timer {
        id: findDelay
        interval: 150
        onTriggered: win.updateFind(true)
    }

    Shortcut {
        sequence: "Ctrl+F"
        onActivated: win.openFind()
    }

    Shortcut {
        sequences: ["Ctrl+G", "F3"]
        enabled: win.findOpen
        onActivated: win.moveFind(1)
    }

    Shortcut {
        sequences: ["Ctrl+Shift+G", "Shift+F3"]
        enabled: win.findOpen
        onActivated: win.moveFind(-1)
    }

    Shortcut {
        sequence: "Escape"
        enabled: win.findOpen && !shortcutsDialog.opened
        onActivated: win.closeFind()
    }

    // As in Omawrite.
    Shortcut {
        sequences: ["F1", "Ctrl+/", "Ctrl+?"]
        onActivated: shortcutsDialog.opened ? shortcutsDialog.close() : shortcutsDialog.open()
    }

    ShortcutsDialog {
        id: shortcutsDialog
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
        sequences: [StandardKey.Print]
        onActivated: backend.printDocument()
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
            y: win.scaled(40) + win.findRoom
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

    // Find in page, as Omawrite's find bar.
    Pane {
        id: findBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: 12
            leftMargin: 12
            rightMargin: 12
        }
        height: win.scaled(56)
        visible: win.findOpen
        z: 10
        leftPadding: 16
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        Material.elevation: 8

        background: Rectangle {
            radius: 9
            color: win.colors.codeBackground
            border.width: 1
            border.color: win.colors.border
        }

        RowLayout {
            anchors.fill: parent
            spacing: 8

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextInput {
                    id: findField
                    anchors.fill: parent
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    clip: true
                    color: win.colors.foreground
                    selectionColor: win.colors.selection
                    selectedTextColor: win.colors.selectionForeground
                    font.family: backend.textFont
                    font.pixelSize: win.scaled(17)
                    onTextChanged: findDelay.restart()
                    Keys.onReturnPressed: (event) => {
                        win.moveFind((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                        event.accepted = true;
                    }
                    Keys.onEnterPressed: (event) => {
                        win.moveFind((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                        event.accepted = true;
                    }
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Find"
                    visible: findField.text.length === 0
                    color: win.colors.muted
                    font.family: backend.textFont
                    font.pixelSize: win.scaled(17)
                }
            }

            Label {
                Layout.preferredWidth: win.scaled(58)
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: win.findMatches.length === 0 ? "0/0" : (win.findIndex + 1) + "/" + win.findMatches.length
                color: win.colors.muted
                font.family: backend.textFont
                font.pixelSize: win.scaled(16)
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 34
                color: win.colors.border
            }

            SearchIconButton {
                iconName: "up"
                iconColor: win.colors.muted
                onClicked: win.moveFind(-1)
            }

            SearchIconButton {
                iconName: "down"
                iconColor: win.colors.muted
                onClicked: win.moveFind(1)
            }

            SearchIconButton {
                iconName: "close"
                iconColor: win.colors.muted
                onClicked: win.closeFind()
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

        // Opening a file and the shortcuts, as Omawrite's footer has them.
        Row {
            id: footerButtons
            anchors {
                left: parent.left
                leftMargin: win.scaled(14)
                verticalCenter: parent.verticalCenter
            }
            spacing: win.scaled(14)

            FooterIconButton {
                objectName: "openButton"
                iconName: "open"
                tooltip: "Open (Ctrl+O)"
                onClicked: openDialog.open()
            }

            FooterIconButton {
                objectName: "shortcutsButton"
                iconName: "keyboard"
                tooltip: "Keyboard shortcuts (F1)"
                onClicked: shortcutsDialog.open()
            }
        }

        // Where a link goes while the pointer is on it, as a browser shows;
        // otherwise the file being read.
        Label {
            anchors {
                left: footerButtons.right
                leftMargin: win.scaled(14)
                right: status.left
                rightMargin: win.scaled(14)
                verticalCenter: parent.verticalCenter
            }
            text: win.hoveredLink || backend.displayPath
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
