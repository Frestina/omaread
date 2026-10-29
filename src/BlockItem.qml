import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// One block of the document, inset by the quotes and list items it sits in,
// with their bars and markers drawn beside it.
Item {
    id: root

    required property var modelData
    required property int index

    readonly property var block: modelData
    readonly property var colors: win.colors
    readonly property int gapTop: win.scaled(block.gap)
    readonly property var containers: block.containers
    readonly property int quoteBar: win.scaled(4)
    readonly property int quoteIndent: quoteBar + win.scaled(16)
    readonly property int itemIndent: win.scaled(32)
    readonly property int textSize: block.small ? win.scaled(12) : win.bodySize
    // A plain quote greys its text; an alert keeps it.
    readonly property bool muted: block.small || containers.some(c => c.kind === "quote" && !c.callout)
    readonly property var offsets: {
        var x = 0;
        var out = [];
        for (var i = 0; i < containers.length; ++i) {
            out.push(x);
            x += containers[i].kind === "quote" ? quoteIndent
               : containers[i].kind === "item" ? itemIndent
               : 0;
        }
        out.push(x);
        return out;
    }
    readonly property int contentX: offsets[offsets.length - 1]
    // Where the first line of text sits, for the markers to line up with.
    readonly property real firstBaseline: content.item && content.item.firstBaseline !== undefined
        ? content.item.firstBaseline : Math.round(textSize * 1.15)

    // Inside a <details> that is closed.
    readonly property bool folded: containers.some(c => c.kind === "details" && !win.detailsOpen(c.id, c.open))

    visible: !folded
    height: folded ? 0 : gapTop + content.height

    // Quote bars. A bar the block before shares runs up through the gap, so
    // a quote reads as one.
    Repeater {
        model: root.containers

        Rectangle {
            required property var modelData
            required property int index

            visible: modelData.kind === "quote"
            x: root.offsets[index]
            y: index < root.block.joinPrevious ? 0 : root.gapTop
            width: root.quoteBar
            height: root.height - y
            color: modelData.callout ? win.calloutColor(modelData.callout) : root.colors.border
        }
    }

    // List markers, on the first block of each item.
    Repeater {
        model: root.containers

        Item {
            required property var modelData
            required property int index

            visible: modelData.kind === "item" && (modelData.marker !== "" || modelData.task >= 0)
            x: root.offsets[index]
            y: root.gapTop
            width: root.itemIndent

            Text {
                id: marker
                visible: parent.modelData.task < 0
                anchors.right: parent.right
                anchors.rightMargin: win.scaled(6)
                y: Math.round(root.firstBaseline - baselineOffset)
                text: parent.modelData.marker
                color: root.muted ? root.colors.muted : root.colors.foreground
                font.family: backend.textFont
                font.pixelSize: root.textSize
            }

            Rectangle {
                visible: parent.modelData.task >= 0
                readonly property int side: Math.round(root.textSize * 0.85)
                anchors.right: parent.right
                anchors.rightMargin: win.scaled(8)
                // Centred on the middle of the lower-case letters.
                y: Math.round(root.firstBaseline - root.textSize * 0.3 - side / 2)
                width: side
                height: side
                radius: win.scaled(3)
                color: parent.modelData.task === 1 ? root.colors.link : "transparent"
                border.width: parent.modelData.task === 1 ? 0 : 1
                border.color: root.colors.muted

                Text {
                    anchors.centerIn: parent
                    visible: parent.parent.modelData.task === 1
                    text: "✓"
                    color: root.colors.background
                    font.pixelSize: Math.round(parent.side * 0.8)
                    font.bold: true
                }
            }
        }
    }

    Loader {
        id: content
        x: root.contentX
        y: root.gapTop
        width: root.width - root.contentX
        sourceComponent: {
            switch (root.block.type) {
            case "heading": return headingBlock;
            case "code": return codeBlock;
            case "rule": return ruleBlock;
            case "table": return tableBlock;
            case "images": return imagesBlock;
            case "calloutTitle": return calloutTitleBlock;
            case "summary": return summaryBlock;
            default: return textBlock;
            }
        }
    }

    function alignment(align) {
        return align === "center" ? TextEdit.AlignHCenter
             : align === "right" ? TextEdit.AlignRight
             : TextEdit.AlignLeft;
    }

    Component {
        id: textBlock

        RichText {
            html: root.block.html
            size: root.textSize
            color: root.muted ? root.colors.muted : root.colors.foreground
            horizontalAlignment: root.alignment(root.block.align)
        }
    }

    Component {
        id: headingBlock

        // GitHub's heading sizes, with a rule under the top two levels.
        Column {
            readonly property int level: root.block.level
            readonly property var sizes: [2, 1.5, 1.25, 1, 0.875, 0.85]
            spacing: win.scaled(Math.round(16 * sizes[level - 1] * 0.3))

            RichText {
                width: parent.width
                html: root.block.html
                size: Math.round(win.bodySize * parent.sizes[parent.level - 1])
                lineHeight: 1.25
                font.weight: Font.Bold
                color: parent.level === 6 || root.muted ? root.colors.muted : root.colors.foreground
            }

            Rectangle {
                visible: parent.level <= 2
                width: parent.width
                height: 1
                color: root.colors.border
            }
        }
    }

    Component {
        id: codeBlock

        // A formula or diagram whose program is missing says what to install
        // under its source.
        Column {
            spacing: win.scaled(8)

            CodeBlock {
                width: parent.width
                block: root.block
            }

            RichText {
                visible: !!root.block.hint
                width: parent.width
                html: root.block.hint || ""
                size: win.scaled(14)
                color: root.colors.muted
            }
        }
    }

    Component {
        id: ruleBlock

        Item {
            implicitHeight: root.block.thin ? 1 : win.scaled(4)

            Rectangle {
                anchors.fill: parent
                color: root.colors.border
            }
        }
    }

    Component {
        id: tableBlock

        TableBlock {
            block: root.block
        }
    }

    Component {
        id: imagesBlock

        ImagesBlock {
            block: root.block
        }
    }

    Component {
        id: calloutTitleBlock

        Row {
            spacing: win.scaled(8)

            CalloutIcon {
                kind: root.block.kind
                color: win.calloutColor(root.block.kind)
                width: win.scaled(16)
                height: win.scaled(16)
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.block.label
                color: win.calloutColor(root.block.kind)
                font.family: backend.textFont
                font.pixelSize: win.bodySize
                font.weight: Font.Bold
            }
        }
    }

    Component {
        id: summaryBlock

        // A <details> summary, with GitHub's triangle; a click anywhere on it
        // opens or closes what it folds.
        Item {
            readonly property bool open: win.detailsOpen(root.block.details, root.block.open)
            readonly property real firstBaseline: summaryText.firstBaseline
            implicitHeight: summaryText.implicitHeight

            // Pointing right when closed and down when open, centred on the
            // middle of the lower-case letters.
            Shape {
                id: triangle
                readonly property real side: Math.round(root.textSize * 0.5)
                x: Math.round(root.textSize * 0.1)
                y: Math.round(summaryText.firstBaseline - root.textSize * 0.3 - side / 2)
                width: side
                height: side
                rotation: parent.open ? 90 : 0
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: "transparent"
                    fillColor: root.muted ? root.colors.muted : root.colors.foreground
                    startX: 0
                    startY: 0
                    PathLine { x: triangle.side; y: triangle.side / 2 }
                    PathLine { x: 0; y: triangle.side }
                    PathLine { x: 0; y: 0 }
                }
            }

            RichText {
                id: summaryText
                x: Math.round(root.textSize * 1.1)
                width: parent.width - x
                html: root.block.html
                size: root.textSize
                color: root.muted ? root.colors.muted : root.colors.foreground
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: win.toggleDetails(root.block.details, root.block.open)
            }
        }
    }
}
