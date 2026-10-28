import QtQuick

// A row of images on its own: a screenshot, a logo or a line of badges. Each
// shows at its own size, or the size the HTML asks for, and never wider than
// the column.
Item {
    id: images

    required property var block
    property real naturalWidth: 0

    implicitHeight: flow.height

    function measure() {
        var total = 0;
        var count = 0;
        for (var i = 0; i < flow.children.length; ++i) {
            var child = flow.children[i];
            if (child.isPicture && child.width > 0) {
                total += child.width;
                ++count;
            }
        }
        naturalWidth = total + Math.max(0, count - 1) * flow.spacing;
    }

    Flow {
        id: flow
        spacing: win.scaled(4)
        width: Math.min(images.width, Math.max(1, images.naturalWidth))
        x: images.block.align === "center" ? Math.round((images.width - width) / 2)
         : images.block.align === "right" ? images.width - width
         : 0

        Repeater {
            model: images.block.images

            Item {
                id: picture

                required property var modelData
                readonly property bool isPicture: true
                // GitHub plays GIFs, which a plain Image would show still.
                readonly property bool animated: /\.(gif|webp)([?#]|$)/i.test(modelData.src)
                readonly property Image image: imageLoader.item || placeholder
                readonly property bool ready: picture.image.status === Image.Ready
                readonly property real aspect: picture.image.implicitWidth > 0
                    ? picture.image.implicitHeight / picture.image.implicitWidth : 0
                // Sizes in the HTML are CSS pixels, so they follow the text
                // size the way a browser's zoom does.
                readonly property real wantedWidth: modelData.width > 0 ? win.scaled(modelData.width)
                    : modelData.height > 0 ? win.scaled(modelData.height) / Math.max(aspect, 0.01)
                    : picture.image.implicitWidth * win.textScale

                width: ready ? Math.min(wantedWidth, images.width) : (picture.image.status === Image.Error ? altText.implicitWidth : 0)
                height: ready ? (modelData.width > 0 && modelData.height > 0
                                 ? width * modelData.height / modelData.width
                                 : width * aspect)
                              : (picture.image.status === Image.Error ? altText.implicitHeight : 0)
                onWidthChanged: images.measure()

                Image {
                    id: placeholder
                    visible: false
                }

                Loader {
                    id: imageLoader
                    anchors.fill: parent
                    sourceComponent: picture.animated ? animatedImage : stillImage
                }

                Component {
                    id: stillImage

                    Image {
                        source: picture.modelData.src
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }
                }

                Component {
                    id: animatedImage

                    AnimatedImage {
                        source: picture.modelData.src
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                }

                Text {
                    id: altText
                    visible: picture.image.status === Image.Error
                    text: picture.modelData.alt || "Image not found"
                    color: win.colors.muted
                    font.family: backend.textFont
                    font.pixelSize: win.bodySize
                    font.italic: true
                }

                HoverHandler {
                    enabled: picture.modelData.href !== ""
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: win.hoveredLink = hovered ? picture.modelData.href : ""
                }

                TapHandler {
                    enabled: picture.modelData.href !== ""
                    onTapped: win.followLink(picture.modelData.href)
                }
            }
        }
    }
}
