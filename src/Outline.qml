import QtQuick
import QtQuick.Controls

// The document's headings, indented by level, as GitHub's outline, shown as
// Contents: a click goes to one, and the section being read is marked and
// kept in view.
Rectangle {
    id: root
    objectName: "outline"

    // [{index, level, text}], index being the heading's block.
    required property var headings
    // The heading being read, as a place in headings, or -1 above the first.
    property int current: -1
    // Over the page, when the window is too narrow to set it beside it.
    property bool floating: false
    // Headings are indented from the shallowest level there is.
    readonly property int topLevel: headings.reduce((level, heading) => Math.min(level, heading.level), 6)

    signal chosen(int index)
    signal closeRequested()

    color: win.colors.background
    border.width: floating ? 1 : 0
    border.color: win.colors.border
    radius: floating ? win.scaled(12) : 0

    onCurrentChanged: {
        if (current >= 0)
            list.positionViewAtIndex(current, ListView.Contain);
    }

    // Beside the page, a rule parts the two.
    Rectangle {
        visible: !root.floating
        anchors {
            right: parent.right
            top: parent.top
            bottom: parent.bottom
        }
        width: 1
        color: win.colors.border
    }

    Item {
        id: header
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: win.scaled(48)

        Label {
            anchors {
                left: parent.left
                leftMargin: win.scaled(16)
                verticalCenter: parent.verticalCenter
            }
            text: "Contents"
            color: win.colors.foreground
            font.family: backend.textFont
            font.pixelSize: win.scaled(14)
            font.weight: Font.DemiBold
        }

        SearchIconButton {
            objectName: "outlineClose"
            anchors {
                right: parent.right
                rightMargin: win.scaled(4)
                verticalCenter: parent.verticalCenter
            }
            iconName: "close"
            iconColor: win.colors.muted
            onClicked: root.closeRequested()
        }
    }

    ListView {
        id: list
        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
            bottom: parent.bottom
            leftMargin: win.scaled(8)
            rightMargin: win.scaled(8)
            bottomMargin: win.scaled(8)
        }
        clip: true
        model: root.headings
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }

        delegate: Rectangle {
            id: entry
            required property var modelData
            required property int index
            readonly property bool isCurrent: index === root.current

            objectName: "outlineEntry" + index

            width: ListView.view.width
            height: label.implicitHeight + win.scaled(12)
            radius: win.scaled(6)
            color: isCurrent || hover.hovered ? win.colors.codeBackground : "transparent"

            // GitHub marks the current entry with a bar of its accent.
            Rectangle {
                visible: entry.isCurrent
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                }
                width: win.scaled(4)
                height: parent.height - win.scaled(8)
                radius: width / 2
                color: win.colors.link
            }

            Label {
                id: label
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: win.scaled(12) + (entry.modelData.level - root.topLevel) * win.scaled(16)
                    rightMargin: win.scaled(8)
                }
                text: entry.modelData.text
                elide: Text.ElideRight
                color: win.colors.foreground
                font.family: backend.textFont
                font.pixelSize: win.scaled(14)
                font.weight: entry.isCurrent ? Font.DemiBold : Font.Normal
            }

            HoverHandler {
                id: hover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: root.chosen(entry.modelData.index)
            }
        }
    }
}
