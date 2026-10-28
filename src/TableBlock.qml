import QtQuick
import QtQuick.Controls

// A GitHub table. Each column is as wide as its widest cell while the table
// fits; past that the widest columns give way and wrap, and a table that
// still does not fit scrolls sideways.
Item {
    id: table

    required property var block
    readonly property int columns: block.aligns.length
    readonly property bool hasHeader: block.header.length > 0
    readonly property var rows: hasHeader ? [block.header].concat(block.rows) : block.rows
    readonly property int padX: win.scaled(13)
    readonly property int padY: win.scaled(6)
    property var widths: []

    implicitHeight: scroller.height

    function cellHtml(row, column) {
        return rows[row][column] || "";
    }

    function offset(column) {
        var x = 0;
        for (var i = 0; i < column; ++i)
            x += widths[i] || 0;
        return x;
    }

    function layout() {
        var natural = [];
        for (var c = 0; c < columns; ++c)
            natural.push(win.scaled(40));
        for (var i = 0; i < measures.count; ++i) {
            var item = measures.itemAt(i);
            if (item)
                natural[i % columns] = Math.max(natural[i % columns], Math.ceil(item.implicitWidth) + 2 * padX + 1);
        }

        // Columns narrower than an even share keep their width; the rest
        // split what is left, and wrap.
        var result = natural.slice();
        var open = [];
        for (c = 0; c < columns; ++c)
            open.push(c);
        var left = table.width;
        var settled = true;
        while (settled && open.length > 0) {
            settled = false;
            var share = left / open.length;
            for (var k = open.length - 1; k >= 0; --k) {
                if (natural[open[k]] <= share) {
                    left -= natural[open[k]];
                    open.splice(k, 1);
                    settled = true;
                }
            }
        }
        for (k = 0; k < open.length; ++k)
            result[open[k]] = Math.max(win.scaled(120), Math.floor(left / open.length));
        widths = result;
    }

    onWidthChanged: layout()
    Component.onCompleted: layout()

    // Each cell's width without wrapping, measured off screen.
    Repeater {
        id: measures
        model: table.rows.length * table.columns

        Text {
            required property int index
            visible: false
            textFormat: Text.RichText
            text: win.richText(table.cellHtml(Math.floor(index / table.columns), index % table.columns),
                               win.bodySize, 1.5)
            font.family: backend.textFont
            font.pixelSize: win.bodySize
            font.weight: table.hasHeader && index < table.columns ? Font.Bold : Font.Normal
            onImplicitWidthChanged: Qt.callLater(table.layout)
        }
    }

    Flickable {
        id: scroller
        width: table.width
        height: grid.height + (interactive ? win.scaled(10) : 0)
        contentWidth: grid.width
        contentHeight: grid.height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width + 1
        clip: true

        ScrollBar.horizontal: ScrollBar {
            policy: scroller.interactive ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: grid
            width: table.offset(table.columns)

            Repeater {
                model: table.rows

                Item {
                    id: row

                    required property var modelData
                    required property int index
                    readonly property bool isHeader: table.hasHeader && index === 0
                    readonly property int bodyIndex: table.hasHeader ? index - 1 : index

                    width: grid.width
                    height: cells.height

                    // Every second body row is shaded, as on GitHub.
                    Rectangle {
                        anchors.fill: parent
                        visible: !row.isHeader && row.bodyIndex % 2 === 1
                        color: win.colors.stripe
                    }

                    Row {
                        id: cells

                        Repeater {
                            model: table.columns

                            Item {
                                id: cell
                                required property int index
                                width: table.widths[index] || 0
                                height: text.implicitHeight + 2 * table.padY

                                RichText {
                                    id: text
                                    x: table.padX
                                    y: table.padY
                                    width: parent.width - 2 * table.padX
                                    html: row.modelData[cell.index] || ""
                                    font.weight: row.isHeader ? Font.Bold : Font.Normal
                                    horizontalAlignment: {
                                        var align = table.block.aligns[cell.index];
                                        return align === "center" ? TextEdit.AlignHCenter
                                             : align === "right" ? TextEdit.AlignRight
                                             : TextEdit.AlignLeft;
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: win.colors.border
                    }

                    Repeater {
                        model: table.columns + 1

                        Rectangle {
                            required property int index
                            x: Math.min(table.offset(index), row.width - 1)
                            width: 1
                            height: row.height
                            color: win.colors.border
                        }
                    }
                }
            }

            Rectangle {
                width: grid.width
                height: 1
                color: win.colors.border
            }
        }
    }
}
