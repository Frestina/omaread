#pragma once

#include <QHash>
#include <QString>

// Code blocks coloured by KSyntaxHighlighting, KDE's highlighter, which knows
// some three hundred languages.
namespace CodeHighlighter {

// The code as HTML, each token in a <span class="…"> for styleSheet to
// colour, so a new palette needs no new HTML. Empty when the language is
// not one it knows; GitHub leaves those plain too.
QString html(const QString &code, const QString &language);

// The colours for those spans: GitHub's, light or dark, or with an Omarchy
// theme the ones its editor themes give each kind of token.
QString styleSheet(bool darkMode, const QHash<QString, QString> &theme = {});

}
