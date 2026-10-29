import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every keyboard shortcut, as Omawrite lists its own, drawn as github.com's
// shortcuts dialog: grouped, each key on a key cap, in the page's colours.
Popup {
    id: root
    objectName: "shortcutsDialog"

    readonly property var sections: [
        { title: "File", shortcuts: [
            { keys: ["Ctrl+O"], desc: "Open a file" },
            { keys: ["Ctrl+P"], desc: "Print" },
            { keys: ["Ctrl+Shift+S"], desc: "Save as PDF" },
            { keys: ["Ctrl+E"], desc: "Edit in Omawrite" },
            { keys: ["Ctrl+R", "F5"], desc: "Reload" },
            { keys: ["Alt+Left", "Backspace"], desc: "Go back" },
            { keys: ["Ctrl+W", "Ctrl+Q"], desc: "Close the window" }
        ] },
        { title: "Find", shortcuts: [
            { keys: ["Ctrl+F"], desc: "Find in page" },
            { keys: ["Enter", "Ctrl+G", "F3"], desc: "Next match" },
            { keys: ["Shift+Enter", "Ctrl+Shift+G", "Shift+F3"], desc: "Previous match" },
            { keys: ["Esc"], desc: "Close find" }
        ] },
        { title: "Reading", shortcuts: [
            { keys: ["Down", "j"], desc: "Scroll down" },
            { keys: ["Up", "k"], desc: "Scroll up" },
            { keys: ["Space", "Page Down"], desc: "Next screen" },
            { keys: ["Shift+Space", "Page Up"], desc: "Previous screen" },
            { keys: ["Home", "g"], desc: "Top" },
            { keys: ["End", "G"], desc: "Bottom" },
            { keys: ["Ctrl+C"], desc: "Copy the selection" }
        ] },
        { title: "View", shortcuts: [
            { keys: ["Ctrl+="], desc: "Larger text" },
            { keys: ["Ctrl+-"], desc: "Smaller text" },
            { keys: ["Ctrl+0"], desc: "Default text size" },
            { keys: ["Ctrl+scroll"], desc: "Change the text size" },
            { keys: ["Ctrl+Shift+O"], desc: "Outline" },
            { keys: ["F11", "Super+F"], desc: "Fullscreen" },
            { keys: ["F1", "Ctrl+/", "Ctrl+?"], desc: "Keyboard shortcuts" }
        ] }
    ]
    readonly property bool wide: availableWidth >= win.scaled(640)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(win.scaled(820), win.width - 32)
    height: Math.min(implicitHeight, win.height - 32)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: win.scaled(24)
    topPadding: win.scaled(16)

    onClosed: flick.forceActiveFocus()

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        color: win.colors.background
        border.width: 1
        border.color: win.colors.border
        radius: win.scaled(12)
    }

    contentItem: ColumnLayout {
        spacing: win.scaled(16)

        RowLayout {
            Layout.fillWidth: true

            Column {
                Layout.fillWidth: true
                spacing: win.scaled(2)

                Label {
                    text: "Keyboard shortcuts"
                    color: win.colors.foreground
                    font.family: backend.textFont
                    font.pixelSize: win.scaled(16)
                    font.weight: Font.DemiBold
                }

                Label {
                    text: "Omaread " + Qt.application.version
                    color: win.colors.muted
                    font.family: backend.textFont
                    font.pixelSize: win.scaled(12)
                }
            }

            SearchIconButton {
                Layout.alignment: Qt.AlignTop
                iconName: "close"
                iconColor: win.colors.muted
                onClicked: root.close()
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: columns.implicitHeight
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true

            GridLayout {
                id: columns
                width: scroll.availableWidth
                columns: root.wide ? 2 : 1
                columnSpacing: win.scaled(32)
                rowSpacing: win.scaled(20)

                // Two columns side by side: File and Find, then Reading and
                // View; one under the other when narrow.
                Repeater {
                    model: [root.sections.slice(0, 2), root.sections.slice(2)]

                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.alignment: Qt.AlignTop
                        spacing: win.scaled(20)

                        Repeater {
                            model: parent.modelData

                            ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0

                                Label {
                                    Layout.bottomMargin: win.scaled(6)
                                    text: parent.modelData.title
                                    color: win.colors.foreground
                                    font.family: backend.textFont
                                    font.pixelSize: win.scaled(14)
                                    font.weight: Font.DemiBold
                                }

                                Repeater {
                                    model: parent.modelData.shortcuts

                                    Rectangle {
                                        required property var modelData
                                        required property int index
                                        Layout.fillWidth: true
                                        implicitHeight: row.implicitHeight + win.scaled(12)
                                        color: "transparent"

                                        Rectangle {
                                            anchors.top: parent.top
                                            width: parent.width
                                            height: 1
                                            color: win.colors.border
                                        }

                                        RowLayout {
                                            id: row
                                            anchors {
                                                left: parent.left
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                            }
                                            spacing: win.scaled(12)

                                            Label {
                                                Layout.fillWidth: true
                                                text: modelData.desc
                                                wrapMode: Text.Wrap
                                                color: win.colors.foreground
                                                font.family: backend.textFont
                                                font.pixelSize: win.scaled(14)
                                            }

                                            Row {
                                                spacing: win.scaled(4)

                                                Repeater {
                                                    model: modelData.keys

                                                    Row {
                                                        required property string modelData
                                                        required property int index
                                                        spacing: win.scaled(4)

                                                        Label {
                                                            anchors.verticalCenter: parent.verticalCenter
                                                            visible: index > 0
                                                            text: "or"
                                                            color: win.colors.muted
                                                            font.family: backend.textFont
                                                            font.pixelSize: win.scaled(12)
                                                        }

                                                        // A key cap, as GitHub's <kbd>.
                                                        Rectangle {
                                                            width: cap.implicitWidth + win.scaled(10)
                                                            height: cap.implicitHeight + win.scaled(6)
                                                            radius: win.scaled(6)
                                                            color: win.colors.codeBackground
                                                            border.width: 1
                                                            border.color: win.colors.border

                                                            Label {
                                                                id: cap
                                                                anchors.centerIn: parent
                                                                text: modelData
                                                                color: win.colors.foreground
                                                                font.family: backend.codeFont
                                                                font.pixelSize: win.scaled(12)
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
