#pragma once

#include <QList>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

// Find in page's marks on one text: every match tinted, the current one
// stronger. They lie over the text's own formatting rather than change it,
// and follow it when the text is set again.
class FindHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    // Where query starts in text, ignoring case and any accents the query
    // leaves out, as Omawrite searches.
    static QList<int> findMatches(const QString &text, const QString &query);

    // Marks query's matches in document, the one starting at current as the
    // current one, and says where they start. An empty query clears them.
    static QList<int> mark(QTextDocument *document, const QString &query, int current, bool darkMode);

protected:
    void highlightBlock(const QString &text) override;

private:
    explicit FindHighlighter(QTextDocument *document);

    QString m_query;
    int m_current = -1;
    bool m_darkMode = false;
    bool m_rehighlighting = false;
};
