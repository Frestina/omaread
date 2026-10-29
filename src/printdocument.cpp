#include "printdocument.h"

#include <QRegularExpression>
#include <QVariantMap>

namespace {

// GitHub's light palette.
const QString foreground = QStringLiteral("#1f2328");
const QString muted = QStringLiteral("#59636e");
const QString link = QStringLiteral("#0969da");
const QString border = QStringLiteral("#d1d9e0");
const QString codeBackground = QStringLiteral("#f6f8fa");
const QHash<QString, QString> calloutColors{{QStringLiteral("note"), QStringLiteral("#0969da")},
                                            {QStringLiteral("tip"), QStringLiteral("#1a7f37")},
                                            {QStringLiteral("important"), QStringLiteral("#8250df")},
                                            {QStringLiteral("warning"), QStringLiteral("#9a6700")},
                                            {QStringLiteral("caution"), QStringLiteral("#d1242f")}};

// GitHub's heading sizes, as multiples of the body text.
constexpr qreal headingSizes[] = {2.0, 1.5, 1.25, 1.0, 0.875, 0.85};
constexpr int barWidth = 4;
constexpr int quoteIndent = 14;
constexpr int markerWidth = 28;

QVariantMap containerAt(const QVariant &block, int depth) {
    const QVariantList containers = block.toMap().value("containers").toList();
    return depth < containers.size() ? containers.at(depth).toMap() : QVariantMap();
}

QString alignAttribute(const QVariant &align) {
    const QString value = align.toString();
    return value.isEmpty() ? QString() : QStringLiteral(" align=\"%1\"").arg(value);
}

// Blocks nest on paper the way they do on screen: quotes as a bar beside
// their content, list items as a marker column beside theirs.
class Writer {
public:
    Writer(const QHash<QString, QSize> &imageSizes, int pageWidth)
        : m_imageSizes(imageSizes), m_pageWidth(pageWidth) {}

    QString write(const QVariantList &blocks) {
        range(blocks, 0, int(blocks.size()), 0, m_pageWidth, false);
        return m_out;
    }

private:
    void range(const QVariantList &blocks, int from, int to, int depth, int width, bool quiet) {
        int index = from;
        while (index < to) {
            const QVariantMap container = containerAt(blocks.at(index), depth);
            // The space above a nested group belongs to the group, not to the
            // first block inside it.
            const int gap = index == from && depth > 0 ? 0 : blocks.at(index).toMap().value("gap").toInt();
            if (container.isEmpty()) {
                block(blocks.at(index).toMap(), width, gap, quiet);
                ++index;
                continue;
            }

            int end = index + 1;
            if (container.value("kind") == QLatin1String("quote")) {
                while (end < to && containerAt(blocks.at(end), depth).value("id") == container.value("id"))
                    ++end;
                quote(blocks, index, end, depth, width, gap, container.value("callout").toString(), quiet);
            } else {
                while (end < to && containerAt(blocks.at(end), depth).value("kind") == QLatin1String("item")
                       && containerAt(blocks.at(end), depth).value("listId") == container.value("listId"))
                    ++end;
                list(blocks, index, end, depth, width, gap, quiet);
            }
            index = end;
        }
    }

    void quote(const QVariantList &blocks, int from, int to, int depth, int width, int gap,
               const QString &callout, bool quiet) {
        const QString bar = callout.isEmpty() ? border : calloutColors.value(callout, border);
        m_out += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top: %1px;\">"
                                "<tr><td width=\"%2\" bgcolor=\"%3\"></td><td width=\"%4\"></td><td>")
                     .arg(gap).arg(barWidth).arg(bar).arg(quoteIndent);
        // A plain quote greys its text; an alert keeps it.
        range(blocks, from, to, depth + 1, width - barWidth - quoteIndent, quiet || callout.isEmpty());
        m_out += QLatin1String("</td></tr></table>");
    }

    void list(const QVariantList &blocks, int from, int to, int depth, int width, int gap, bool quiet) {
        // Full width, or the item column shrinks to its content and wraps early.
        m_out += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top: %1px;\">")
                     .arg(gap);
        int index = from;
        while (index < to) {
            const QVariantMap item = containerAt(blocks.at(index), depth);
            int end = index + 1;
            while (end < to && containerAt(blocks.at(end), depth).value("id") == item.value("id"))
                ++end;

            const int task = item.value("task").toInt();
            // Noto Sans has no ballot boxes, and the font Qt falls back to
            // draws ☑ as a colour emoji; Noto Sans Symbols 2 draws both as text.
            const QString box = QStringLiteral("<span style=\"font-family: 'Noto Sans Symbols 2';\">%1</span>");
            const QString marker = task == 1 ? box.arg(QStringLiteral("☑"))
                : task == 0 ? box.arg(QStringLiteral("☐"))
                : item.value("marker").toString().toHtmlEscaped();
            const QVariantMap first = blocks.at(index).toMap();
            const int itemGap = index == from ? 0 : first.value("gap").toInt();
            const QString size = first.value("small").toBool() ? QStringLiteral(" font-size: 9pt;") : QString();
            m_out += QStringLiteral("<tr><td width=\"%1\" align=\"right\" valign=\"top\" "
                                    "style=\"padding-top: %2px; padding-right: 6px; color: %3;%4\">%5</td>"
                                    "<td valign=\"top\" style=\"padding-top: %2px;\">")
                         .arg(markerWidth).arg(itemGap).arg(quiet ? muted : foreground, size, marker);
            range(blocks, index, end, depth + 1, width - markerWidth, quiet);
            m_out += QLatin1String("</td></tr>");
            index = end;
        }
        m_out += QLatin1String("</table>");
    }

    void block(const QVariantMap &block, int width, int gap, bool quiet) {
        const QString type = block.value("type").toString();
        const bool small = block.value("small").toBool();
        QString style = QStringLiteral("margin-top: %1px; margin-bottom: 0px;").arg(gap);
        if (quiet || small)
            style += QStringLiteral(" color: %1;").arg(muted);
        if (small)
            style += QLatin1String(" font-size: 9pt;");

        if (type == QLatin1String("heading")) {
            const int level = qBound(1, block.value("level").toInt(), 6);
            style += QStringLiteral(" font-size: %1pt; font-weight: bold;")
                         .arg(m_bodyPoints * headingSizes[level - 1], 0, 'f', 1);
            if (level == 6)
                style += QStringLiteral(" color: %1;").arg(muted);
            m_out += QStringLiteral("<p style=\"%1\">%2</p>").arg(style, block.value("html").toString());
            // GitHub rules off the top two levels.
            if (level <= 2)
                m_out += QStringLiteral("<hr style=\"margin-top: 4px; background-color: %1;\">").arg(border);
        } else if (type == QLatin1String("code")) {
            m_out += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"%1\" "
                                    "style=\"margin-top: %2px;\"><tr><td>"
                                    "<p style=\"margin: 0px; white-space: pre-wrap;\"><code>%3</code></p>"
                                    "</td></tr></table>")
                         .arg(codeBackground).arg(gap).arg(block.value("code").toString().toHtmlEscaped());
        } else if (type == QLatin1String("rule")) {
            m_out += QStringLiteral("<hr style=\"margin-top: %1px; background-color: %2;\">").arg(gap).arg(border);
        } else if (type == QLatin1String("table")) {
            table(block, gap);
        } else if (type == QLatin1String("images")) {
            m_out += QStringLiteral("<p%1 style=\"%2\">").arg(alignAttribute(block.value("align")), style);
            for (const QVariant &image : block.value("images").toList())
                m_out += imageTag(image.toMap(), width) + QLatin1Char(' ');
            m_out += QLatin1String("</p>");
        } else if (type == QLatin1String("calloutTitle")) {
            m_out += QStringLiteral("<p style=\"%1 color: %2; font-weight: bold;\">%3</p>")
                         .arg(style, calloutColors.value(block.value("kind").toString(), foreground),
                              block.value("label").toString().toHtmlEscaped());
        } else {
            m_out += QStringLiteral("<p%1 style=\"%2\">%3</p>")
                         .arg(alignAttribute(block.value("align")), style, block.value("html").toString());
        }
    }

    void table(const QVariantMap &block, int gap) {
        const QStringList aligns = block.value("aligns").toStringList();
        const QStringList header = block.value("header").toStringList();
        // Qt draws collapsed borders cell by cell, so each cell names its own.
        m_out += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"6\" "
                                "style=\"margin-top: %1px; border-collapse: collapse;\">")
                     .arg(gap);
        const auto row = [this, &aligns](const QStringList &cells, const QString &tag, bool shaded) {
            m_out += shaded ? QStringLiteral("<tr bgcolor=\"%1\">").arg(codeBackground) : QStringLiteral("<tr>");
            for (int column = 0; column < cells.size(); ++column) {
                m_out += QStringLiteral("<%1%2 style=\"border: 1px solid %3; padding-left: 12px; padding-right: 12px;\">%4</%1>")
                             .arg(tag, alignAttribute(aligns.value(column)), border, cells.at(column));
            }
            m_out += QLatin1String("</tr>");
        };
        if (!header.isEmpty())
            row(header, QStringLiteral("th"), false);
        const QVariantList rows = block.value("rows").toList();
        for (int index = 0; index < rows.size(); ++index)
            row(rows.at(index).toStringList(), QStringLiteral("td"), index % 2 == 1);
        m_out += QLatin1String("</table>");
    }

    // At the size the page asks for, or the image's own, never wider than
    // the text.
    QString imageTag(const QVariantMap &image, int width) const {
        const QString source = image.value("src").toString();
        const QSize natural = m_imageSizes.value(source);
        if (natural.isEmpty())
            return image.value("alt").toString().toHtmlEscaped();

        qreal w = image.value("width").toInt();
        qreal h = image.value("height").toInt();
        if (w <= 0 && h <= 0) {
            w = natural.width();
            h = natural.height();
        } else if (w <= 0) {
            w = h * natural.width() / natural.height();
        } else if (h <= 0) {
            h = w * natural.height() / natural.width();
        }
        if (w > width) {
            h = h * width / w;
            w = width;
        }
        return QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\">")
            .arg(source.toHtmlEscaped())
            .arg(qRound(w))
            .arg(qRound(h));
    }

    const QHash<QString, QSize> &m_imageSizes;
    const int m_pageWidth;
    const qreal m_bodyPoints = 11;
    QString m_out;
};

}

QStringList PrintDocument::imageSources(const QVariantList &blocks) {
    static const QRegularExpression inlineImage(QStringLiteral(R"re(<img\b[^>]*?\bsrc="([^"]*)")re"),
                                                QRegularExpression::CaseInsensitiveOption);
    QStringList sources;
    const auto addFromHtml = [&sources](const QString &html) {
        auto it = inlineImage.globalMatch(html);
        while (it.hasNext()) {
            // The renderer escaped the source for HTML.
            QString source = it.next().captured(1);
            source.replace(QLatin1String("&amp;"), QLatin1String("&"));
            source.replace(QLatin1String("&quot;"), QLatin1String("\""));
            sources.append(source);
        }
    };

    for (const QVariant &value : blocks) {
        const QVariantMap block = value.toMap();
        for (const QVariant &image : block.value("images").toList())
            sources.append(image.toMap().value("src").toString());
        addFromHtml(block.value("html").toString());
        for (const QVariant &cell : block.value("header").toList())
            addFromHtml(cell.toString());
        for (const QVariant &row : block.value("rows").toList()) {
            for (const QVariant &cell : row.toList())
                addFromHtml(cell.toString());
        }
    }
    sources.removeDuplicates();
    sources.removeAll(QString());
    return sources;
}

QString PrintDocument::html(const QVariantList &blocks, const QHash<QString, QSize> &imageSizes,
                            int pageWidth) {
    return QStringLiteral("<html><body style=\"color: %1;\">").arg(foreground)
        + Writer(imageSizes, pageWidth).write(blocks) + QLatin1String("</body></html>");
}

QString PrintDocument::styleSheet(const QString &codeFont) {
    return QStringLiteral("a { color: %1; text-decoration: none; }"
                          " code { font-family: '%2'; font-size: 9pt; background-color: %3; }"
                          " th { font-weight: bold; }")
        .arg(link, codeFont, codeBackground);
}
