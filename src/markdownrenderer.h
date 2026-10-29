#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// A Markdown document as the list of blocks Main.qml lays out, one map per
// heading, paragraph, code block, table, row of images or rule.
struct RenderedMarkdown {
    QVariantList blocks;
    // Heading and footnote anchors, to the index of the block each one names.
    QHash<QString, int> anchors;
    // How many formulas and diagrams it asked for pictures of.
    int figures = 0;
};

// Parses with cmark-gfm, the parser github.com uses, with the same
// extensions, so a file reads the way it does there.
namespace MarkdownRenderer {
// The picture of a formula ("math") or diagram ("mermaid"), as {src, width,
// height}, its size in CSS pixels at 16px text; empty when there is none, and
// then its source shows instead.
using FigureLookup = std::function<QVariantMap(const QString &kind, const QString &source, bool display)>;

// Relative image paths resolve against baseUrl, the file's folder, and paths
// starting with '/' against rootUrl, the repository root, as on GitHub.
RenderedMarkdown render(const QByteArray &markdown, const QUrl &baseUrl,
                        const QUrl &rootUrl = QUrl(), const FigureLookup &figure = {});

// GitHub's anchor for a heading: lower case, punctuation dropped, spaces as
// hyphens.
QString slugify(const QString &text);

QUrl resolveUrl(const QString &url, const QUrl &baseUrl, const QUrl &rootUrl);

}
