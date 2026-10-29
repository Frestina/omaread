#include "markdownrenderer.h"

#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cstring>
#include <optional>

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

namespace {

// github.com's extensions. tagfilter only acts on cmark's own HTML output;
// the HTML here is filtered in cleanHtml instead.
const char *const extensionNames[] = {"table", "strikethrough", "autolink", "tagfilter", "tasklist"};

// GitHub's alerts, `> [!NOTE]` and friends, in the order GitHub lists them.
const QStringList calloutKinds{QStringLiteral("note"), QStringLiteral("tip"),
                               QStringLiteral("important"), QStringLiteral("warning"),
                               QStringLiteral("caution")};

// Space above a block, in pixels at text scale 1, as github.com's style sheet
// spaces them.
constexpr int paragraphGap = 16;
constexpr int headingGap = 24;
constexpr int ruleGap = 24;
constexpr int tightItemGap = 4;
constexpr int calloutTitleGap = 8;
// A <details> shows its content straight under the summary, as on GitHub.
constexpr int summaryGap = 0;

QString literal(cmark_node *node) {
    const char *text = cmark_node_get_literal(node);
    return text ? QString::fromUtf8(text) : QString();
}

QString typeName(cmark_node *node) {
    return QString::fromLatin1(cmark_node_get_type_string(node));
}

QString url(cmark_node *node) {
    const char *text = cmark_node_get_url(node);
    return text ? QString::fromUtf8(text) : QString();
}

// GitHub numbers nested ordered lists 1, i, a.
QString roman(int number) {
    static const QList<QPair<int, QString>> numerals{
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"},
        {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}};
    QString out;
    for (const auto &[value, numeral] : numerals) {
        while (number >= value) {
            out += numeral;
            number -= value;
        }
    }
    return out;
}

QString alphabetic(int number) {
    QString out;
    while (number > 0) {
        --number;
        out.prepend(QChar('a' + number % 26));
        number /= 26;
    }
    return out;
}

// The attributes of one HTML tag, names in lower case.
QHash<QString, QString> tagAttributes(const QString &tag) {
    static const QRegularExpression attribute(
        QStringLiteral(R"re(([A-Za-z][\w:-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))re"));
    QHash<QString, QString> attributes;
    auto it = attribute.globalMatch(tag);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        QString value = match.captured(2);
        if (value.isNull())
            value = match.captured(3);
        if (value.isNull())
            value = match.captured(4);
        attributes.insert(match.captured(1).toLower(), value);
    }
    return attributes;
}

struct Emoji {
    const char *shortcode;
    const char *emoji;
};

// GitHub's shortcodes, sorted.
const Emoji emojis[] = {
#include "emojidata.inc"
};

// `:tada:` as 🎉, as GitHub writes it; a shortcode it does not know stays as
// written.
QString emojify(const QString &text) {
    if (!text.contains(QLatin1Char(':')))
        return text;
    static const QRegularExpression shortcode(QStringLiteral(R"re(:([\w+-]+):)re"));
    QString out;
    qsizetype last = 0;
    auto it = shortcode.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QByteArray name = match.captured(1).toUtf8();
        const Emoji *end = std::end(emojis);
        const Emoji *found = std::lower_bound(std::begin(emojis), end, name,
            [](const Emoji &emoji, const QByteArray &name) { return std::strcmp(emoji.shortcode, name.constData()) < 0; });
        if (found == end || name != found->shortcode)
            continue;
        out += text.mid(last, match.capturedStart() - last) + QString::fromUtf8(found->emoji);
        last = match.capturedEnd();
    }
    return out + text.mid(last);
}

// The same in HTML, in its text alone: not in tags, and not in code.
QString emojifyHtml(const QString &html) {
    static const QRegularExpression skipped(
        QStringLiteral(R"re(<(code|pre)\b.*?</\1\s*>|<[^>]*>)re"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    QString out;
    qsizetype last = 0;
    auto it = skipped.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += emojify(html.mid(last, match.capturedStart() - last)) + match.captured();
        last = match.capturedEnd();
    }
    return out + emojify(html.mid(last));
}

struct Container {
    QString kind;        // "quote", "item" or "details"
    int id = 0;
    QString callout;     // quote: the GitHub alert it is, if any
    QString marker;      // item: "•", "1." and so on; empty for a task
    int task = -1;       // item: -1 not a task, 0 open, 1 done
    int listId = 0;      // item: the list it belongs to
    bool tight = true;   // item: whether that list is tight
    bool fresh = true;   // item: nothing shown inside it yet, so the marker is due
    bool open = false;   // details: open to begin with
};

class Builder {
public:
    Builder(const QUrl &baseUrl, const QUrl &rootUrl) : m_baseUrl(baseUrl), m_rootUrl(rootUrl) {}

    void document(cmark_node *document) {
        children(document);
        flushDetails();
        finish();
    }

    RenderedMarkdown result;

private:
    void children(cmark_node *parent) {
        for (cmark_node *node = cmark_node_first_child(parent); node; node = cmark_node_next(node))
            block(node);
    }

    void block(cmark_node *node) {
        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_HEADING:
            heading(node);
            return;
        case CMARK_NODE_PARAGRAPH:
            paragraph(node);
            return;
        case CMARK_NODE_BLOCK_QUOTE:
            quote(node);
            return;
        case CMARK_NODE_LIST:
            list(node);
            return;
        case CMARK_NODE_CODE_BLOCK:
            code(node);
            return;
        case CMARK_NODE_HTML_BLOCK:
            htmlBlock(node);
            return;
        case CMARK_NODE_THEMATIC_BREAK:
            add({{"type", "rule"}});
            return;
        case CMARK_NODE_FOOTNOTE_DEFINITION:
            footnote(node);
            return;
        default:
            break;
        }

        if (typeName(node) == QLatin1String("table"))
            table(node);
        else
            children(node);
    }

    void heading(cmark_node *node) {
        QString anchor = MarkdownRenderer::slugify(plainText(node));
        // A repeated heading gets -1, -2 and so on, as on GitHub.
        if (m_slugs.contains(anchor)) {
            int suffix = 1;
            while (m_slugs.contains(anchor + QLatin1Char('-') + QString::number(suffix)))
                ++suffix;
            anchor += QLatin1Char('-') + QString::number(suffix);
        }
        m_slugs.insert(anchor);
        m_pendingAnchors.append(anchor);

        add({{"type", "heading"},
             {"level", cmark_node_get_heading_level(node)},
             {"html", inlines(node)},
             {"anchor", anchor}});
    }

    void paragraph(cmark_node *node) {
        QVariantList images;
        if (imageParagraph(node, images)) {
            add({{"type", "images"}, {"images", images}, {"align", currentAlign()}});
            return;
        }
        add({{"type", "paragraph"}, {"html", inlines(node)}, {"align", currentAlign()}});
    }

    // A paragraph of nothing but images, linked or not: a screenshot, or a row
    // of badges. Shown as images sized to the column rather than inline in text.
    bool imageParagraph(cmark_node *paragraph, QVariantList &images) {
        for (cmark_node *node = cmark_node_first_child(paragraph); node; node = cmark_node_next(node)) {
            switch (cmark_node_get_type(node)) {
            case CMARK_NODE_IMAGE:
                images.append(image(url(node), plainText(node), QString()));
                break;
            case CMARK_NODE_LINK: {
                const QString href = url(node);
                for (cmark_node *child = cmark_node_first_child(node); child; child = cmark_node_next(child)) {
                    if (cmark_node_get_type(child) == CMARK_NODE_IMAGE)
                        images.append(image(url(child), plainText(child), href));
                    else if (!isBlank(child))
                        return false;
                }
                break;
            }
            default:
                if (!isBlank(node))
                    return false;
            }
        }
        return !images.isEmpty();
    }

    static bool isBlank(cmark_node *node) {
        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_SOFTBREAK:
        case CMARK_NODE_LINEBREAK:
            return true;
        case CMARK_NODE_TEXT:
            return literal(node).trimmed().isEmpty();
        default:
            return false;
        }
    }

    QVariantMap image(const QString &source, const QString &alt, const QString &href,
                      int width = 0, int height = 0) const {
        return {{"src", imageUrl(source).toString()},
                {"alt", alt},
                {"href", href},
                {"width", width},
                {"height", height}};
    }

    QUrl imageUrl(const QString &source) const {
        QUrl resolved = MarkdownRenderer::resolveUrl(source, m_baseUrl, m_rootUrl);
        // A link to an image's page on github.com shows the image there.
        if (resolved.host() == QLatin1String("github.com")
            && resolved.path().contains(QLatin1String("/blob/"))) {
            resolved.setPath(resolved.path().replace(QLatin1String("/blob/"), QLatin1String("/raw/")));
        }
        return resolved;
    }

    void quote(cmark_node *node) {
        Container container;
        container.kind = QStringLiteral("quote");
        container.id = m_nextId++;
        container.callout = takeCalloutMarker(node);
        flushDetails();
        const qsizetype depth = m_containers.size();
        m_containers.append(container);

        if (!container.callout.isEmpty()) {
            QString label = container.callout;
            label[0] = label[0].toUpper();
            add({{"type", "calloutTitle"}, {"kind", container.callout}, {"label", label}});
        }
        children(node);
        // A <details> left open inside the quote closes with it.
        flushDetails();
        m_containers.resize(depth);
    }

    // The alert a quote opens with, `[!NOTE]` alone on its first line, taken
    // out of the text so only the title shows it.
    QString takeCalloutMarker(cmark_node *quote) {
        cmark_node *paragraph = cmark_node_first_child(quote);
        if (!paragraph || cmark_node_get_type(paragraph) != CMARK_NODE_PARAGRAPH)
            return {};

        QString line;
        QList<cmark_node *> markerNodes;
        cmark_node *node = cmark_node_first_child(paragraph);
        for (; node && cmark_node_get_type(node) == CMARK_NODE_TEXT; node = cmark_node_next(node)) {
            line += literal(node);
            markerNodes.append(node);
        }

        static const QRegularExpression marker(
            QStringLiteral(R"re(^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]$)re"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = marker.match(line.trimmed());
        if (!match.hasMatch())
            return {};

        const bool lineEnds = node && (cmark_node_get_type(node) == CMARK_NODE_SOFTBREAK
                                       || cmark_node_get_type(node) == CMARK_NODE_LINEBREAK);
        // A marker with nothing after it is just a quote that says "[!NOTE]".
        if (!lineEnds && (node || !cmark_node_next(paragraph)))
            return {};

        if (lineEnds)
            markerNodes.append(node);
        for (cmark_node *markerNode : markerNodes) {
            cmark_node_unlink(markerNode);
            cmark_node_free(markerNode);
        }
        if (!cmark_node_first_child(paragraph)) {
            cmark_node_unlink(paragraph);
            cmark_node_free(paragraph);
        }
        return match.captured(1).toLower();
    }

    void list(cmark_node *node) {
        const int listId = m_nextId++;
        const bool ordered = cmark_node_get_list_type(node) == CMARK_ORDERED_LIST;
        const bool tight = cmark_node_get_list_tight(node);
        int depth = 0;
        for (const Container &container : std::as_const(m_containers)) {
            if (container.kind == QLatin1String("item"))
                ++depth;
        }

        flushDetails();
        const qsizetype outer = m_containers.size();
        int number = cmark_node_get_list_start(node);
        for (cmark_node *item = cmark_node_first_child(node); item; item = cmark_node_next(item), ++number) {
            Container container;
            container.kind = QStringLiteral("item");
            container.id = m_nextId++;
            container.listId = listId;
            container.tight = tight;
            if (typeName(item) == QLatin1String("tasklist"))
                container.task = cmark_gfm_extensions_get_tasklist_item_checked(item) ? 1 : 0;
            else if (ordered)
                container.marker = (depth == 0 ? QString::number(number)
                                    : depth == 1 ? roman(number) : alphabetic(number))
                    + QLatin1Char('.');
            else
                container.marker = depth == 0 ? QStringLiteral("•")
                    : depth == 1 ? QStringLiteral("◦") : QStringLiteral("▪");

            m_containers.append(container);
            children(item);
            // A <details> left open inside the item closes with it.
            flushDetails();
            m_containers.resize(outer + 1);
            // An empty item still shows its marker.
            if (m_containers.constLast().fresh)
                add({{"type", "paragraph"}, {"html", QString()}});
            m_containers.removeLast();
        }
    }

    void code(cmark_node *node) {
        QString text = literal(node);
        if (text.endsWith(QLatin1Char('\n')))
            text.chop(1);
        const char *info = cmark_node_get_fence_info(node);
        const QString language = info ? QString::fromUtf8(info).section(QLatin1Char(' '), 0, 0) : QString();
        add({{"type", "code"}, {"code", text}, {"language", language}});
    }

    void table(cmark_node *node) {
        const int columns = cmark_gfm_extensions_get_table_columns(node);
        const uint8_t *alignments = cmark_gfm_extensions_get_table_alignments(node);
        QVariantList aligns;
        for (int column = 0; column < columns; ++column) {
            switch (alignments ? alignments[column] : 0) {
            case 'l': aligns.append(QStringLiteral("left")); break;
            case 'c': aligns.append(QStringLiteral("center")); break;
            case 'r': aligns.append(QStringLiteral("right")); break;
            default: aligns.append(QString());
            }
        }

        QVariantList header;
        QVariantList rows;
        for (cmark_node *row = cmark_node_first_child(node); row; row = cmark_node_next(row)) {
            QVariantList cells;
            for (cmark_node *cell = cmark_node_first_child(row); cell; cell = cmark_node_next(cell))
                cells.append(inlines(cell));
            while (cells.size() < columns)
                cells.append(QString());
            if (typeName(row) == QLatin1String("table_header"))
                header = cells;
            else
                rows.append(QVariant(cells));
        }
        add({{"type", "table"}, {"aligns", aligns}, {"header", header}, {"rows", rows}});
    }

    // <details> folds away what follows it, Markdown or HTML, up to
    // </details>, leaving its <summary> to open it with.
    void htmlBlock(cmark_node *node) {
        static const QRegularExpression detailsTag(
            QStringLiteral(R"re(<(details)\b([^>]*)>|<(/details)\s*>|<summary\b[^>]*>(.*?)</summary\s*>)re"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        const QString raw = literal(node);
        qsizetype last = 0;
        auto it = detailsTag.globalMatch(raw);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            htmlFragment(raw.mid(last, match.capturedStart() - last));
            last = match.capturedEnd();
            if (!match.captured(1).isEmpty())
                openDetails(match.captured(2));
            else if (!match.captured(3).isEmpty())
                closeDetails();
            else
                summary(cleanHtml(match.captured(4)));
        }
        htmlFragment(raw.mid(last));
    }

    void openDetails(const QString &attributes) {
        flushDetails();
        static const QRegularExpression open(QStringLiteral(R"re(\bopen\b)re"),
                                             QRegularExpression::CaseInsensitiveOption);
        Container details;
        details.kind = QStringLiteral("details");
        details.id = m_nextId++;
        details.open = open.match(attributes).hasMatch();
        m_pendingDetails = details;
    }

    // The summary shows whether its <details> is open or not; the blocks
    // after it go inside.
    void summary(const QString &html) {
        if (!m_pendingDetails) {
            add({{"type", "paragraph"}, {"html", html}, {"align", currentAlign()}});
            return;
        }
        const Container details = *m_pendingDetails;
        m_pendingDetails.reset();
        add({{"type", "summary"}, {"html", html}, {"details", details.id}, {"open", details.open}});
        m_containers.append(details);
    }

    // A <details> without a <summary> is called "Details", as on GitHub.
    void flushDetails() {
        if (m_pendingDetails)
            summary(QStringLiteral("Details"));
    }

    void closeDetails() {
        flushDetails();
        // Only a <details> opened at this level closes here; one opened in a
        // quote or list item has closed with it.
        if (!m_containers.isEmpty() && m_containers.constLast().kind == QLatin1String("details"))
            m_containers.removeLast();
    }

    void htmlFragment(const QString &raw) {
        // `<div align="center">` on its own, then Markdown, then `</div>`:
        // GitHub centres what is between, so carry the alignment over.
        static const QRegularExpression opening(
            QStringLiteral(R"re(^\s*<(div|p|h[1-6])\b([^>]*)>\s*$)re"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression closing(
            QStringLiteral(R"re(^\s*</(div|p|h[1-6])\s*>\s*$)re"),
            QRegularExpression::CaseInsensitiveOption);
        if (const QRegularExpressionMatch match = opening.match(raw); match.hasMatch()) {
            m_alignStack.append(tagAttributes(match.captured(2)).value(QStringLiteral("align")).toLower());
            return;
        }
        if (closing.match(raw).hasMatch()) {
            if (!m_alignStack.isEmpty())
                m_alignStack.removeLast();
            return;
        }

        const QString html = cleanHtml(raw);
        static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
        QString text = html;
        text.remove(tags).replace(QLatin1String("&nbsp;"), QLatin1String(" "));
        const bool textless = text.trimmed().isEmpty();

        QVariantList images = htmlImages(html);
        if (textless && images.isEmpty())
            return;

        static const QRegularExpression alignAttribute(
            QStringLiteral(R"re(\balign\s*=\s*["']?(left|center|right))re"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch alignMatch = alignAttribute.match(html);
        const QString align = alignMatch.hasMatch() ? alignMatch.captured(1).toLower() : currentAlign();

        if (textless) {
            add({{"type", "images"}, {"images", images}, {"align", align}});
            return;
        }
        add({{"type", "paragraph"}, {"html", html}, {"align", align}});
    }

    // The <img> tags in a piece of HTML, with the link each sits in.
    QVariantList htmlImages(const QString &html) const {
        static const QRegularExpression tag(
            QStringLiteral(R"re(<(/?)(a|img)\b([^>]*)>)re"), QRegularExpression::CaseInsensitiveOption);
        QVariantList images;
        QString href;
        auto it = tag.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const bool isClosing = !match.captured(1).isEmpty();
            const QString name = match.captured(2).toLower();
            const QHash<QString, QString> attributes = tagAttributes(match.captured(3));
            if (name == QLatin1String("a")) {
                href = isClosing ? QString() : attributes.value(QStringLiteral("href"));
                continue;
            }
            // cleanHtml has resolved the sources already.
            images.append(QVariantMap{{"src", attributes.value(QStringLiteral("src"))},
                                      {"alt", attributes.value(QStringLiteral("alt"))},
                                      {"href", href},
                                      {"width", attributes.value(QStringLiteral("width")).toInt()},
                                      {"height", attributes.value(QStringLiteral("height")).toInt()}});
        }
        return images;
    }

    // HTML a README carries, made safe and fit for Qt's rich text: scripts and
    // styles go, image sources resolve, and <details> shows open, since rich
    // text has nothing to fold it with.
    QString cleanHtml(QString html) const {
        static const QRegularExpression dropped(
            QStringLiteral(R"re(<(script|style|iframe|textarea|title|noembed|noframes|xmp|plaintext)\b.*?</\1\s*>)re"),
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression comments(
            QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression details(
            QStringLiteral(R"re(</?details\b[^>]*>)re"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression summaryOpen(
            QStringLiteral(R"re(<summary\b[^>]*>)re"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression summaryClose(
            QStringLiteral(R"re(</summary\s*>)re"), QRegularExpression::CaseInsensitiveOption);
        html.remove(dropped);
        html.remove(comments);
        html.remove(details);
        html.replace(summaryOpen, QStringLiteral("<p><b>▾ "));
        html.replace(summaryClose, QStringLiteral("</b></p>"));

        static const QRegularExpression source(
            QStringLiteral(R"re((<img\b[^>]*?\bsrc\s*=\s*)(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))re"),
            QRegularExpression::CaseInsensitiveOption);
        QString out;
        qsizetype last = 0;
        auto it = source.globalMatch(html);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            QString value = match.captured(2);
            if (value.isNull())
                value = match.captured(3);
            if (value.isNull())
                value = match.captured(4);
            out += html.mid(last, match.capturedStart() - last);
            out += match.captured(1) + QLatin1Char('"')
                + imageUrl(value).toString().toHtmlEscaped() + QLatin1Char('"');
            last = match.capturedEnd();
        }
        out += html.mid(last);
        return emojifyHtml(out.trimmed());
    }

    void footnote(cmark_node *node) {
        // GitHub sets the footnotes apart under a thin rule, in small print.
        if (!m_footnoteListId) {
            m_footnoteListId = m_nextId++;
            add({{"type", "rule"}, {"thin", true}});
        }

        // cmark-gfm has put the notes in the order they are first referenced.
        const int number = ++m_footnoteCount;
        Container container;
        container.kind = QStringLiteral("item");
        container.id = m_nextId++;
        container.listId = m_footnoteListId;
        container.marker = QString::number(number) + QLatin1Char('.');
        m_containers.append(container);
        m_pendingAnchors.append(QStringLiteral("fn-%1").arg(number));

        ++m_footnoteDepth;
        const qsizetype first = result.blocks.size();
        children(node);
        --m_footnoteDepth;
        m_containers.removeLast();

        // The way back to where the note was referenced, after its last paragraph.
        for (qsizetype index = result.blocks.size() - 1; index >= first; --index) {
            QVariantMap block = result.blocks.at(index).toMap();
            if (block.value("type") != QLatin1String("paragraph"))
                continue;
            block["html"] = block.value("html").toString()
                + QStringLiteral(" <a href=\"#fnref-%1\">↩</a>").arg(number);
            result.blocks[index] = block;
            break;
        }
    }

    QString currentAlign() const {
        return m_alignStack.isEmpty() ? QString() : m_alignStack.constLast();
    }

    QString inlines(cmark_node *parent) {
        QString out;
        // Text comes in runs of nodes, split where cmark tried a `_` or `*`
        // that came to nothing; a shortcode can span them.
        QString text;
        for (cmark_node *node = cmark_node_first_child(parent); node; node = cmark_node_next(node)) {
            if (cmark_node_get_type(node) == CMARK_NODE_TEXT) {
                text += literal(node);
                continue;
            }
            out += emojify(text).toHtmlEscaped();
            text.clear();
            inlineNode(node, out);
        }
        return out + emojify(text).toHtmlEscaped();
    }

    // Inline Markdown, all but text, as the HTML subset Qt's rich text
    // understands.
    void inlineNode(cmark_node *node, QString &out) {
        switch (cmark_node_get_type(node)) {
        case CMARK_NODE_SOFTBREAK:
            out += QLatin1Char(' ');
            return;
        case CMARK_NODE_LINEBREAK:
            out += QLatin1String("<br>");
            return;
        case CMARK_NODE_CODE:
            out += QLatin1String("<code>") + literal(node).toHtmlEscaped() + QLatin1String("</code>");
            return;
        case CMARK_NODE_HTML_INLINE:
            out += cleanHtml(literal(node));
            return;
        case CMARK_NODE_EMPH:
            out += QLatin1String("<i>") + inlines(node) + QLatin1String("</i>");
            return;
        case CMARK_NODE_STRONG:
            out += QLatin1String("<b>") + inlines(node) + QLatin1String("</b>");
            return;
        case CMARK_NODE_LINK:
            out += QLatin1String("<a href=\"") + url(node).toHtmlEscaped() + QLatin1String("\">")
                + inlines(node) + QLatin1String("</a>");
            return;
        case CMARK_NODE_IMAGE:
            out += QLatin1String("<img src=\"") + imageUrl(url(node)).toString().toHtmlEscaped()
                + QLatin1String("\" alt=\"") + plainText(node).toHtmlEscaped() + QLatin1String("\">");
            return;
        case CMARK_NODE_FOOTNOTE_REFERENCE: {
            // cmark-gfm numbers references for us, in the order they come.
            const int number = literal(node).toInt();
            if (!m_referencedFootnotes.contains(number)) {
                m_referencedFootnotes.insert(number);
                m_pendingAnchors.append(QStringLiteral("fnref-%1").arg(number));
            }
            out += QStringLiteral("<sup><a href=\"#fn-%1\">%1</a></sup>").arg(number);
            return;
        }
        default:
            break;
        }

        if (typeName(node) == QLatin1String("strikethrough"))
            out += QLatin1String("<s>") + inlines(node) + QLatin1String("</s>");
        else
            out += inlines(node);
    }

    static QString plainText(cmark_node *parent) {
        QString text;
        for (cmark_node *node = cmark_node_first_child(parent); node; node = cmark_node_next(node)) {
            switch (cmark_node_get_type(node)) {
            case CMARK_NODE_TEXT:
            case CMARK_NODE_CODE:
                text += literal(node);
                break;
            case CMARK_NODE_SOFTBREAK:
            case CMARK_NODE_LINEBREAK:
                text += QLatin1Char(' ');
                break;
            default:
                text += plainText(node);
            }
        }
        return text;
    }

    // Every block carries the quotes and list items it sits in, outermost
    // first, so the view can draw their bars and markers beside it.
    void add(QVariantMap block) {
        if (block.value("type") != QLatin1String("summary"))
            flushDetails();
        QVariantList containers;
        for (Container &container : m_containers) {
            const bool showMarker = container.kind == QLatin1String("item") && container.fresh;
            containers.append(QVariantMap{{"kind", container.kind},
                                          {"id", container.id},
                                          {"callout", container.callout},
                                          {"listId", container.listId},
                                          {"tight", container.tight},
                                          {"marker", showMarker ? container.marker : QString()},
                                          {"task", showMarker ? container.task : -1},
                                          {"open", container.open}});
            container.fresh = false;
        }
        block["containers"] = containers;
        block["small"] = m_footnoteDepth > 0;

        for (const QString &anchor : std::as_const(m_pendingAnchors))
            result.anchors.insert(anchor, int(result.blocks.size()));
        m_pendingAnchors.clear();
        result.blocks.append(block);
    }

    // The space above each block, and how many of its containers it shares
    // with the block before, so a quote's bar runs on unbroken.
    void finish() {
        for (qsizetype index = 0; index < result.blocks.size(); ++index) {
            QVariantMap block = result.blocks.at(index).toMap();
            int gap = 0;
            int shared = 0;
            if (index > 0) {
                const QVariantMap previous = result.blocks.at(index - 1).toMap();
                shared = sharedContainers(previous, block);
                gap = gapBetween(previous, block);
            }
            block["gap"] = gap;
            block["joinPrevious"] = shared;
            result.blocks[index] = block;
        }
    }

    static int sharedContainers(const QVariantMap &previous, const QVariantMap &block) {
        const QVariantList before = previous.value("containers").toList();
        const QVariantList now = block.value("containers").toList();
        int shared = 0;
        while (shared < before.size() && shared < now.size()
               && before.at(shared).toMap().value("id") == now.at(shared).toMap().value("id"))
            ++shared;
        return shared;
    }

    static int gapBetween(const QVariantMap &previous, const QVariantMap &block) {
        const QString type = block.value("type").toString();
        const QString previousType = previous.value("type").toString();
        if (type == QLatin1String("heading"))
            return headingGap;
        if (type == QLatin1String("rule") || previousType == QLatin1String("rule"))
            return ruleGap;
        if (previousType == QLatin1String("heading"))
            return paragraphGap;
        if (previousType == QLatin1String("calloutTitle"))
            return calloutTitleGap;
        if (previousType == QLatin1String("summary")
            && sharedContainers(previous, block) < block.value("containers").toList().size())
            return summaryGap;

        // Blocks in the same list, in the same item or the next: a tight list
        // keeps them close.
        const QVariantList before = previous.value("containers").toList();
        const QVariantList now = block.value("containers").toList();
        for (qsizetype depth = qMin(before.size(), now.size()) - 1; depth >= 0; --depth) {
            const QVariantMap a = before.at(depth).toMap();
            const QVariantMap b = now.at(depth).toMap();
            if (b.value("kind") != QLatin1String("item") || a.value("listId") != b.value("listId"))
                continue;
            return b.value("tight").toBool() ? tightItemGap : paragraphGap;
        }
        return paragraphGap;
    }

    QUrl m_baseUrl;
    QUrl m_rootUrl;
    QList<Container> m_containers;
    QStringList m_pendingAnchors;
    QSet<QString> m_slugs;
    QSet<int> m_referencedFootnotes;
    int m_footnoteCount = 0;
    QStringList m_alignStack;
    // A <details> waiting for its <summary>.
    std::optional<Container> m_pendingDetails;
    int m_footnoteListId = 0;
    int m_footnoteDepth = 0;
    int m_nextId = 1;
};

}

RenderedMarkdown MarkdownRenderer::render(const QByteArray &markdown, const QUrl &baseUrl,
                                          const QUrl &rootUrl) {
    cmark_gfm_core_extensions_ensure_registered();
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_FOOTNOTES | CMARK_OPT_VALIDATE_UTF8);
    for (const char *name : extensionNames) {
        if (cmark_syntax_extension *extension = cmark_find_syntax_extension(name))
            cmark_parser_attach_syntax_extension(parser, extension);
    }
    cmark_parser_feed(parser, markdown.constData(), size_t(markdown.size()));
    cmark_node *document = cmark_parser_finish(parser);

    Builder builder(baseUrl, rootUrl);
    builder.document(document);

    cmark_node_free(document);
    cmark_parser_free(parser);
    return builder.result;
}

QString MarkdownRenderer::slugify(const QString &text) {
    QString slug;
    for (const QChar character : text.trimmed().toLower()) {
        if (character.isLetterOrNumber() || character.isMark() || character == QLatin1Char('_')
            || character == QLatin1Char('-'))
            slug += character;
        else if (character == QLatin1Char(' '))
            slug += QLatin1Char('-');
    }
    return slug;
}

QUrl MarkdownRenderer::resolveUrl(const QString &url, const QUrl &baseUrl, const QUrl &rootUrl) {
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return {};
    if (trimmed.startsWith(QLatin1String("//")))
        return QUrl(QStringLiteral("https:") + trimmed);

    const QUrl parsed(trimmed);
    if (!parsed.scheme().isEmpty())
        return parsed;
    if (trimmed.startsWith(QLatin1Char('/')) && rootUrl.isValid())
        return rootUrl.resolved(QUrl(trimmed.mid(1)));
    return baseUrl.resolved(parsed);
}
