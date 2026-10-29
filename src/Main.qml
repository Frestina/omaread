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
    readonly property int columnWidth: Math.max(0, Math.min(scaled(880), flick.width - 2 * scaled(32)))
    // The headings, for the outline, as {index, level, text}.
    readonly property var headings: backend.blocks.reduce((out, block, index) => {
        if (block.type === "heading")
            out.push({ index: index, level: block.level, text: block.text });
        return out;
    }, [])
    readonly property bool outlineShown: backend.outlineOpen && headings.length > 0
    // Whatever had the keys in the outline, the page has them once it goes.
    onOutlineShownChanged: {
        if (!outlineShown && !findOpen)
            flick.forceActiveFocus();
    }
    readonly property int outlineWidth: scaled(296)
    // The outline sits beside the page while the text keeps a comfortable
    // measure, and over it in a narrower window.
    readonly property bool outlineDocked: width - outlineWidth >= scaled(640) + 2 * scaled(32)
    property string hoveredLink: ""
    property var selectionOwner: null
    // The block being read when the file was about to be shown again, as
    // {index, offset, height}, or {top: true} at the very top.
    property var readingPlace: null
    // The place the page holds while blocks above it change height, as
    // images load, until the reader scrolls: {index, offset}, or {top: true}.
    property var heldPlace: null
    property bool placing: false
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
        // A place the reader goes to wins over one being restored.
        readingPlace = null;
        heldPlace = null;
        glide.stop();
        wheelScroll.stop();
        flick.cancelFlick();
        flick.contentY = Math.max(0, Math.min(y, maxY()));
    }

    // The block at the top of the window, and how far into it the window is.
    function placeOnScreen() {
        if (flick.contentY <= 0)
            return { top: true };
        for (var i = 0; i < blocks.count; ++i) {
            var item = blocks.itemAt(i);
            var top = page.y + item.y;
            if (item.height > 0 && top + item.height > flick.contentY)
                return { index: i, offset: flick.contentY - top, height: item.height };
        }
        return null;
    }

    // Back to the place read before the file changed: the same block, or the
    // one now in its place, the same way into it.
    function restorePlace() {
        var place = readingPlace;
        readingPlace = null;
        if (!place)
            return;
        if (place.top) {
            holdPlace({ top: true });
            return;
        }
        var follow = backend.followBlock(place.index);
        var item = blocks.itemAt(follow.index);
        if (!item) {
            holdPlace({ top: true });
            return;
        }
        // Into a changed block by the same share of it.
        var offset = follow.same ? Math.min(place.offset, item.height)
                                 : place.offset / place.height * item.height;
        holdPlace({ index: follow.index, offset: offset });
    }

    // Puts the held place at the top of the window again.
    function holdPlace(place) {
        if (place !== undefined)
            heldPlace = place;
        if (!heldPlace || placing)
            return;
        // New blocks are placed when the page is next laid out; place them
        // now, to measure.
        placing = true;
        page.forceLayout();
        placing = false;
        var item = heldPlace.top ? null : blocks.itemAt(heldPlace.index);
        if (!heldPlace.top && !item) {
            heldPlace = null;
            return;
        }
        flick.cancelFlick();
        flick.contentY = item ? Math.max(0, Math.min(page.y + item.y + heldPlace.offset, maxY())) : 0;
    }

    // The heading being read: the last one at or above the top of the
    // window, or at the very bottom, the last one on screen.
    function currentHeading() {
        var top = flick.contentY + findRoom + scaled(24);
        var atBottom = flick.contentY >= maxY() - 1 && maxY() > 0;
        var current = -1;
        for (var i = 0; i < headings.length; ++i) {
            var item = blocks.itemAt(headings[i].index);
            if (!item || item.folded)
                continue;
            var y = page.y + item.y + item.gapTop;
            if (y <= top || (atBottom && y < flick.contentY + flick.height))
                current = i;
            else if (!atBottom)
                break;
        }
        return current;
    }

    // Glides to a block, as the outline goes to a heading, opening any
    // <details> it is folded in first; it then holds as images above load.
    function glideToBlock(index) {
        page.forceLayout();
        var item = blocks.itemAt(index);
        if (!item)
            return;
        if (item.folded) {
            unfold(item.block);
            Qt.callLater(glideToBlock, index);
            return;
        }
        readingPlace = null;
        heldPlace = null;
        flick.cancelFlick();
        wheelScroll.stop();
        glide.stop();
        glide.index = index;
        glide.to = Math.max(0, Math.min(page.y + item.y + item.gapTop - scaled(16), maxY()));
        glide.start();
    }

    function scrollToAnchor(anchor) {
        page.forceLayout();
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
        // It stays there as images above it load.
        holdPlace({ index: index, offset: item.gapTop - scaled(16) });
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
        heldPlace = null;
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
        heldPlace = null;
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
            win.readingPlace = win.placeOnScreen();
            win.heldPlace = null;
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
                Qt.callLater(win.restorePlace);
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

    Shortcut {
        sequence: "Ctrl+Shift+O"
        enabled: win.headings.length > 0
        onActivated: backend.outlineOpen = !backend.outlineOpen
    }

    // An outline over the page closes as a popup would.
    Shortcut {
        sequence: "Escape"
        enabled: win.outlineShown && !win.outlineDocked && !win.findOpen && !shortcutsDialog.opened
        onActivated: backend.outlineOpen = false
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

    // The PDF printing would make, saved without the print dialog: the
    // picker suggests the file's own name and folder.
    Dialogs.FileDialog {
        id: pdfDialog
        objectName: "pdfDialog"
        title: "Save as PDF"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["PDF (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: backend.savePdf(selectedFile)
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        enabled: backend.blocks.length > 0
        onActivated: {
            var url = backend.pdfUrl();
            pdfDialog.currentFolder = url.toString().replace(/\/[^\/]*$/, "");
            pdfDialog.selectedFile = url;
            pdfDialog.open();
        }
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
        objectName: "flick"
        anchors {
            left: parent.left
            right: win.outlineShown && win.outlineDocked ? outline.left : parent.right
            top: parent.top
            bottom: footer.top
        }
        contentWidth: width
        contentHeight: page.y + page.height + win.scaled(64)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        focus: true

        ScrollBar.vertical: ScrollBar {
            // The wheel moves contentY itself rather than flicking, so the
            // bar is told it is in use.
            active: hovered || pressed || wheelScroll.running || scrollLinger.running
            onPressedChanged: {
                if (pressed)
                    win.heldPlace = null;
            }
        }

        Timer {
            id: scrollLinger
            interval: 600
        }

        // The page holds its place as what is above it changes height, until
        // the reader scrolls.
        onContentHeightChanged: {
            win.holdPlace();
            outline.current = win.currentHeading();
        }
        onContentYChanged: outline.current = win.currentHeading()
        onMovementStarted: {
            wheelScroll.stop();
            glide.stop();
            win.readingPlace = null;
            win.heldPlace = null;
        }

        NumberAnimation {
            id: glide
            property int index: -1
            target: flick
            property: "contentY"
            duration: 250
            easing.type: Easing.OutCubic
            // Images above may have moved it meanwhile; the heading holds.
            onFinished: {
                var item = blocks.itemAt(index);
                if (item)
                    win.holdPlace({ index: index, offset: item.gapTop - win.scaled(16) });
            }
        }

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

        // Scrolling as Omawrite does it. Flickable moves a touchpad's
        // scroll less than half the way the fingers go, and a wheel notch by
        // a flick sized by the small application font, which crawls next to
        // a browser. A touchpad moves the page as far as the fingers instead,
        // and a notch follows Chromium's wheel physics
        // (cc::ScrollOffsetAnimationCurve): 3 lines of 40px towards a running
        // target, quicker the more is outstanding, and a notch landing
        // mid-animation carries the current velocity into the new curve, so
        // sustained spinning picks up speed.
        readonly property real wheelStep: win.scaled(120)

        FrameAnimation {
            id: wheelScroll
            running: false

            property real startY: 0
            property real targetY: 0
            property real duration: 0.2
            // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a fresh
            // scroll, with y1 tilted on retarget so the curve's initial slope
            // matches the velocity it inherits.
            property real cx1: 0.42
            property real cy1: 0
            readonly property real cx2: 0.58
            readonly property real cy2: 1

            onTriggered: {
                var x = elapsedTime / duration;
                if (x >= 1) {
                    flick.contentY = flick.snapToPixel(targetY);
                    stop();
                    return;
                }
                flick.contentY = flick.snapToPixel(startY + (targetY - startY) * curveY(solveCurve(x)));
            }

            function begin(from, to, dur, slope) {
                startY = from;
                targetY = to;
                duration = dur;
                cx1 = 0.42;
                cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                restart();
            }

            function retarget(newTarget) {
                var s = solveCurve(Math.min(1, elapsedTime / duration));
                var pos = startY + (targetY - startY) * curveY(s);
                var delta = newTarget - pos;
                if (Math.abs(delta) < 0.5) {
                    flick.contentY = newTarget;
                    stop();
                    return;
                }
                var velocity = curveDY(s) / Math.max(1e-6, curveDX(s)) * (targetY - startY) / duration;
                var dur = flick.wheelDuration(delta);
                // When already moving faster than the eased curve would,
                // bound the duration by the time to target at the current
                // velocity; the 2.5x covers the ease-out tail.
                if (velocity !== 0 && delta / velocity > 0)
                    dur = Math.min(dur, delta / velocity * 2.5);
                begin(pos, newTarget, dur, velocity * dur / delta);
            }

            // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
            // evaluated by Newton-solving the curve parameter from x.
            function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
            function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
            function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
            function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

            function solveCurve(x) {
                var s = x;
                for (var i = 0; i < 8; ++i) {
                    var error = curveX(s) - x;
                    if (Math.abs(error) < 0.001)
                        break;
                    var d = curveDX(s);
                    if (Math.abs(d) < 1e-6)
                        break;
                    s = Math.max(0, Math.min(1, s - error / d));
                }
                return s;
            }
        }

        WheelHandler {
            // Wayland compositors route every pointer's scroll through one
            // seat device that Qt classifies as a touchpad, so the device type
            // cannot tell a mouse wheel from two-finger scrolling. The events
            // can: wheel notches carry only angleDelta, while finger scrolling
            // carries pixel-precise pixelDelta.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (wheel) => {
                if (wheel.modifiers & Qt.ControlModifier) {
                    flick.zoomByWheel(wheel);
                    wheel.accepted = true;
                    return;
                }

                // The reader is scrolling: no place is held for them.
                glide.stop();
                win.readingPlace = null;
                win.heldPlace = null;
                scrollLinger.restart();
                if (wheel.pixelDelta.y !== 0) {
                    wheelScroll.stop();
                    flick.contentY = flick.clampContentY(flick.contentY - wheel.pixelDelta.y);
                } else {
                    flick.scrollByWheel(wheel);
                }
                wheel.accepted = true;
            }
        }

        // Ctrl+wheel changes the text size, as it does in a browser. A wheel
        // notch is a whole step, but a touchpad trickles in pixels, so the
        // fractions are banked and a step taken once a whole one is scrolled.
        property real zoomNotches: 0

        function zoomByWheel(wheel) {
            // Finger scrolling synthesizes an angleDelta too, so pixelDelta is
            // the one to test first.
            var notches = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y / 50 : wheel.angleDelta.y / 120;
            if (zoomNotches * notches < 0)
                zoomNotches = 0;
            zoomNotches += notches;
            while (zoomNotches >= 1) {
                backend.increaseTextSize();
                zoomNotches -= 1;
            }
            while (zoomNotches <= -1) {
                backend.decreaseTextSize();
                zoomNotches += 1;
            }
        }

        function scrollByWheel(wheel) {
            // High-resolution wheels report fractional notches; they take the
            // same animated path, as Chromium does for every wheel event.
            var notches = wheel.angleDelta.y / 120;
            if (notches === 0)
                return;
            if (wheelScroll.running) {
                wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                return;
            }
            var target = clampContentY(contentY - notches * wheelStep);
            if (target !== contentY)
                wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
        }

        // Chromium's inverse-delta duration: 200ms for a single notch,
        // ramping down to 100ms once 480px are outstanding.
        function wheelDuration(delta) {
            var pixels = Math.abs(delta) / win.textScale;
            return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
        }

        function clampContentY(y) {
            return Math.max(0, Math.min(win.maxY(), y));
        }

        // Whole device pixels keep natively hinted glyphs from
        // re-rasterizing mid-animation, which reads as shimmer.
        function snapToPixel(y) {
            return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
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
            // Blocks can move without the page changing height.
            onPositioningComplete: win.holdPlace()

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
            right: flick.right
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

    Outline {
        id: outline
        anchors {
            right: parent.right
            top: parent.top
            bottom: footer.top
            margins: floating ? win.scaled(12) : 0
        }
        width: win.outlineWidth
        visible: win.outlineShown
        floating: !win.outlineDocked
        z: floating ? 5 : 0
        headings: win.headings
        onChosen: (index) => win.glideToBlock(index)
        onCloseRequested: backend.outlineOpen = false
        Component.onCompleted: current = win.currentHeading()
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
                objectName: "outlineButton"
                iconName: "outline"
                active: win.outlineShown
                enabled: win.headings.length > 0
                tooltip: enabled ? "Contents (Ctrl+Shift+O)" : "No headings"
                onClicked: backend.outlineOpen = !backend.outlineOpen
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
