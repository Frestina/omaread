#pragma once

#include <QHash>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>

// The rendered blocks as HTML for a QTextDocument to print, in GitHub's
// light colours whatever the screen shows, since paper is white.
namespace PrintDocument {

// Every image the page shows, so they can be loaded before it is laid out.
QStringList imageSources(const QVariantList &blocks);

// pageWidth is the width of the printed text, in the 96 dpi pixels the
// document is laid out in; images wider than that are scaled to fit.
QString html(const QVariantList &blocks, const QHash<QString, QSize> &imageSizes, int pageWidth);

QString styleSheet(const QString &codeFont);

}
