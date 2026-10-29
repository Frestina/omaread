#include "figures.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

const QString mathjaxDir = QStringLiteral("/usr/share/mathjax");

// Pictures are drawn at twice the size they show, so they are sharp on a
// fine screen; paper draws them again from the SVG.
constexpr int mathScale = 2;

// A program that has not finished in this long is given up on.
constexpr int drawingTimeout = 60000;

QString node() {
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

}

Figures::Figures(QObject *parent)
    : QObject(parent),
      m_dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/figures")) {
    QDir().mkpath(m_dir);
}

bool Figures::canDraw(const QString &kind) {
    if (kind == QLatin1String("math"))
        return !node().isEmpty() && QFile::exists(mathjaxDir + QStringLiteral("/tex-svg.js"));
    return false;
}

// What of the page's colours a figure is drawn in.
QString Figures::style(const QString &kind, const QVariantMap &colors, bool darkMode) const {
    Q_UNUSED(kind);
    Q_UNUSED(darkMode);
    return colors.value(QStringLiteral("foreground")).toString();
}

QString Figures::file(const Figure &figure) const {
    return m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".svg");
}

QVariantMap Figures::picture(const QString &kind, const QString &source, bool display,
                             const QVariantMap &colors, bool darkMode) {
    Figure figure{kind, QString(), source, display, style(kind, colors, darkMode)};
    figure.key = QString::fromLatin1(
        QCryptographicHash::hash(QStringList{kind, QString::number(display), QString::number(mathScale),
                                             figure.style, source}
                                     .join(QLatin1Char('\n')).toUtf8(),
                                 QCryptographicHash::Sha1)
            .toHex()
            .left(24));

    const QString path = file(figure);
    if (QFile::exists(path)) {
        const QSize size = QImageReader(path).size();
        if (size.isValid())
            return {{"src", QUrl::fromLocalFile(path).toString()},
                    {"width", qreal(size.width()) / mathScale},
                    {"height", qreal(size.height()) / mathScale}};
    }
    if (!canDraw(kind) || QFile::exists(m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".failed")))
        return {};
    if (!m_drawing.contains(figure.key))
        m_wanted.insert(figure.key, figure);
    return {};
}

void Figures::drawWanted() {
    // What is wanted meanwhile waits for these to finish.
    if (m_wanted.isEmpty() || !m_processes.isEmpty())
        return;

    QHash<QString, QList<Figure>> byKind;
    for (const Figure &figure : std::as_const(m_wanted)) {
        byKind[figure.kind].append(figure);
        m_drawing.insert(figure.key);
    }
    m_wanted.clear();

    for (auto it = byKind.cbegin(); it != byKind.cend(); ++it) {
        const QList<Figure> figures = it.value();
        QProcess *process = start(it.key(), figures);
        m_processes.append(process);
        connect(process, &QProcess::finished, this, [this, process, figures]() {
            finish(process, figures);
            if (m_processes.isEmpty()) {
                emit drawn();
                drawWanted();
            }
        });
        // One that cannot start, or hangs, finishes all the same.
        connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                emit process->finished(-1, QProcess::CrashExit);
        });
        QTimer::singleShot(drawingTimeout, process, &QProcess::kill);
    }
}

void Figures::drawWantedNow(int timeout) {
    for (QProcess *process : std::as_const(m_processes))
        process->waitForFinished(timeout);
    QHash<QString, QList<Figure>> byKind;
    for (const Figure &figure : std::as_const(m_wanted))
        byKind[figure.kind].append(figure);
    m_wanted.clear();
    for (auto it = byKind.cbegin(); it != byKind.cend(); ++it) {
        QProcess *process = start(it.key(), it.value());
        process->waitForFinished(timeout);
        finish(process, it.value());
    }
}

QProcess *Figures::start(const QString &kind, const QList<Figure> &figures) {
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    Q_UNUSED(kind);

    QFile script(QStringLiteral(":/drawmath.cjs"));
    if (!script.open(QIODevice::ReadOnly))
        qWarning("Omaread's math script is missing");
    QJsonArray formulas;
    for (const Figure &figure : figures) {
        formulas.append(QJsonObject{{"key", figure.key},
                                    {"tex", figure.source},
                                    {"display", figure.display},
                                    {"color", figure.style}});
    }
    process->start(node(), {QStringLiteral("-e"), QString::fromUtf8(script.readAll()), mathjaxDir, m_dir,
                            QString::number(mathScale)});
    process->write(QJsonDocument(formulas).toJson(QJsonDocument::Compact));
    process->closeWriteChannel();
    return process;
}

// A figure the program did not draw is marked as failed, so it is not asked
// for again; it shows as its source.
void Figures::finish(QProcess *process, const QList<Figure> &figures) {
    for (const Figure &figure : figures) {
        m_drawing.remove(figure.key);
        const QString failed = m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".failed");
        if (!QFile::exists(file(figure)) && !QFile::exists(failed)) {
            QFile marker(failed);
            if (!marker.open(QIODevice::WriteOnly))
                qWarning("Could not write %s", qPrintable(failed));
        }
    }
    m_processes.removeOne(process);
    process->deleteLater();
}
