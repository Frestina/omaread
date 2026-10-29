#include "findhighlighter.h"

#include <QColor>
#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

// Each UTF-16 unit case-folded and, for Latin and Greek letters, stripped of
// its canonical diacritics: 'Ț' becomes 't'. Other scripts keep their marks,
// since there they tell letters apart (и and й, か and が), and Hangul
// syllables would fold to their first jamo. Surrogates map to themselves here
// and are folded in pairs by searchFolded.
const char16_t *searchFoldTable() {
    static const std::vector<char16_t> table = [] {
        std::vector<char16_t> folded(0x10000);
        for (int unit = 0; unit < 0x10000; ++unit) {
            const QChar original{char16_t(unit)};
            if (original.isSurrogate()) {
                folded[unit] = char16_t(unit);
                continue;
            }
            const QChar caseFolded = original.toCaseFolded();
            QChar base = caseFolded;
            while (base.decompositionTag() == QChar::Canonical) {
                const QChar next = base.decomposition().at(0);
                if (next.isSurrogate())
                    break;
                base = next.toCaseFolded();
            }
            const bool strips = base.script() == QChar::Script_Latin
                || base.script() == QChar::Script_Greek;
            folded[unit] = strips ? base.unicode() : caseFolded.unicode();
        }
        // A stroke or a missing dot is part of the letter, not a mark on it,
        // so these have no decomposition to strip.
        const std::pair<char16_t, char16_t> plain[] = {
            {u'ł', u'l'}, {u'đ', u'd'}, {u'ø', u'o'},
            {u'ħ', u'h'}, {u'ı', u'i'}};
        for (char16_t &unit : folded) {
            for (const auto &[from, to] : plain) {
                if (unit == from)
                    unit = to;
            }
        }
        return folded;
    }();
    return table.data();
}

// Romanian is written with ș and ț but much text has the older ş and ţ.
QChar commaBelow(QChar c) {
    if (c == QChar(0x015F))
        return QChar(0x0219);
    if (c == QChar(0x0163))
        return QChar(0x021B);
    return c;
}

// The text folded unit by unit through the table, then each surrogate pair
// case-folded as the one letter it encodes, which stays a pair: 𐐀 becomes 𐐨.
QString searchFolded(const QString &text) {
    const char16_t *fold = searchFoldTable();
    QString folded(text.size(), Qt::Uninitialized);
    const QChar *source = text.constData();
    QChar *target = folded.data();
    for (qsizetype i = 0; i < text.size(); ++i)
        target[i] = QChar(fold[source[i].unicode()]);
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (!source[i].isHighSurrogate() || !source[i + 1].isLowSurrogate())
            continue;
        const char32_t caseFolded = QChar::toCaseFolded(QChar::surrogateToUcs4(source[i], source[i + 1]));
        if (QChar::requiresSurrogates(caseFolded)) {
            target[i] = QChar(QChar::highSurrogate(caseFolded));
            target[i + 1] = QChar(QChar::lowSurrogate(caseFolded));
        }
        ++i;
    }
    return folded;
}

}

FindHighlighter::FindHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {
    // Setting a text's HTML again, for a new palette or text size, has Qt
    // mark it while it is still empty; mark it again once the new text is in.
    connect(document, &QTextDocument::contentsChanged, this, [this]() {
        if (m_rehighlighting || m_query.isEmpty())
            return;
        QMetaObject::invokeMethod(this, [this]() {
            m_rehighlighting = true;
            rehighlight();
            m_rehighlighting = false;
        }, Qt::QueuedConnection);
    });
}

QList<int> FindHighlighter::findMatches(const QString &text, const QString &query) {
    QList<int> matches;
    const qsizetype length = query.size();
    if (length == 0 || text.size() < length)
        return matches;

    const QString foldedQuery = searchFolded(query);
    // The query's accented letters, case-folded, which the text must match
    // as written rather than folded.
    QList<std::pair<qsizetype, QChar>> exact;
    for (qsizetype i = 0; i < length; ++i) {
        const QChar unit = query.at(i);
        const QChar caseFolded = unit.toCaseFolded();
        if (!unit.isSurrogate() && foldedQuery.at(i) != caseFolded)
            exact.append({i, commaBelow(caseFolded)});
    }

    const QString foldedText = searchFolded(text);

    qsizetype from = 0;
    while ((from = foldedText.indexOf(foldedQuery, from)) >= 0) {
        const bool accentsMatch = std::all_of(exact.cbegin(), exact.cend(), [&](const auto &letter) {
            return commaBelow(text.at(from + letter.first).toCaseFolded()) == letter.second;
        });
        if (accentsMatch) {
            matches.append(int(from));
            from += length;
        } else {
            ++from;
        }
    }
    return matches;
}

QList<int> FindHighlighter::mark(QTextDocument *document, const QString &query, int current, bool darkMode) {
    if (!document)
        return {};
    auto *highlighter = document->findChild<FindHighlighter *>(QString(), Qt::FindDirectChildrenOnly);
    if (!highlighter && query.isEmpty())
        return {};
    if (!highlighter)
        highlighter = new FindHighlighter(document);
    if (highlighter->m_query != query || highlighter->m_current != current
        || highlighter->m_darkMode != darkMode) {
        highlighter->m_query = query;
        highlighter->m_current = current;
        highlighter->m_darkMode = darkMode;
        highlighter->rehighlight();
    }

    // A match does not run from one paragraph into the next.
    QList<int> starts;
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (const int from : findMatches(block.text(), query))
            starts.append(block.position() + from);
    }
    return starts;
}

void FindHighlighter::highlightBlock(const QString &text) {
    if (m_query.isEmpty())
        return;
    // Omawrite's colours, so the pair find alike.
    const QColor match(m_darkMode ? QStringLiteral("#725b18") : QStringLiteral("#ffe58a"));
    const QColor current(m_darkMode ? QStringLiteral("#b36b20") : QStringLiteral("#ffad42"));
    // Only the background: Qt lays this over the text's own format.
    for (const int from : findMatches(text, m_query)) {
        QTextCharFormat format;
        format.setBackground(currentBlock().position() + from == m_current ? current : match);
        setFormat(from, int(m_query.size()), format);
    }
}
