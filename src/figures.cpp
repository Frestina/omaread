#include "figures.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <utility>

#include <unistd.h>

namespace {

const QString mathjaxDir = QStringLiteral("/usr/share/mathjax");

// Pictures are drawn larger than they show, so they are sharp on a fine
// screen. Paper draws formulas again from their SVG, but diagrams are
// pictures, drawn larger still for it.
constexpr int mathScale = 2;
constexpr int mermaidScale = 3;

// How long a picture is kept, in days.
constexpr int cacheDays = 30;

int scale(const QString &kind) {
    return kind == QLatin1String("math") ? mathScale : mermaidScale;
}

// Stops a program and what it started, all in a process group of their own.
// mmdc stops its Chromium when asked to, so they are asked first, then made to.
void stop(QObject *context, QProcess *process) {
    const pid_t group = pid_t(process->processId());
    if (group <= 0)
        return;
    ::kill(-group, SIGTERM);
    QTimer::singleShot(2000, context, [group]() { ::kill(-group, SIGKILL); });
}

QString node() {
    return QStandardPaths::findExecutable(QStringLiteral("node"));
}

QString mmdc() {
    return QStandardPaths::findExecutable(QStringLiteral("mmdc"));
}

// Draws each diagram, one at a time so that one that is not Mermaid spoils
// no other, and keeps why a diagram failed in its .failed file.
const QString drawMermaid = QStringLiteral(
    "cd \"$1\" && shift && scale=$1 && shift\n"
    "for key; do\n"
    "  if mmdc -q -i \"$key.mmd\" -o \"$key.png\" -s \"$scale\" -b transparent -c \"$key.json\" 2>\"$key.err\"; then\n"
    "    rm -f \"$key.err\"\n"
    "  else\n"
    "    mv \"$key.err\" \"$key.failed\"\n"
    "  fi\n"
    "  rm -f \"$key.mmd\" \"$key.json\"\n"
    "done\n");

}

Figures::Figures(QObject *parent)
    : QObject(parent),
      m_dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/figures")) {
    QDir().mkpath(m_dir);
    // Each edit of a figure, and each set of colours, draws a picture of its
    // own; what has not been drawn for a while goes, and is drawn again if
    // it is still wanted. A figure marked failed is tried again then too.
    const QDateTime old = QDateTime::currentDateTime().addDays(-cacheDays);
    const QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files);
    for (const QFileInfo &info : files) {
        if (info.lastModified() < old)
            QFile::remove(info.absoluteFilePath());
    }
}

Figures::~Figures() {
    for (QProcess *process : std::as_const(m_processes)) {
        if (process->processId() > 0)
            ::kill(-pid_t(process->processId()), SIGTERM);
    }
}

bool Figures::canDraw(const QString &kind) {
    if (kind == QLatin1String("math"))
        return !node().isEmpty() && QFile::exists(mathjaxDir + QStringLiteral("/tex-svg.js"));
    if (kind == QLatin1String("mermaid"))
        return !mmdc().isEmpty();
    return false;
}

// What of the page's colours a figure is drawn in: a formula in the text's
// colour, and a diagram in Mermaid's own light or dark theme on GitHub's
// palettes, as github.com draws them, or in Mermaid's base theme coloured
// from an Omarchy theme.
QString Figures::style(const QString &kind, const QVariantMap &colors, bool darkMode) {
    const auto color = [&colors](const char *name) { return colors.value(QLatin1String(name)).toString(); };
    if (kind == QLatin1String("math"))
        return color("foreground");

    QJsonObject variables{{"fontFamily", "Noto Sans, sans-serif"}};
    QString theme = darkMode ? QStringLiteral("dark") : QStringLiteral("default");
    const QString background = color("background");
    if (background != QLatin1String("#ffffff") && background != QLatin1String("#0d1117")) {
        theme = QStringLiteral("base");
        variables.insert("darkMode", darkMode);
        variables.insert("background", background);
        variables.insert("primaryColor", color("codeBackground"));
        variables.insert("primaryTextColor", color("foreground"));
        variables.insert("primaryBorderColor", color("muted"));
        variables.insert("secondaryColor", color("stripe"));
        variables.insert("tertiaryColor", background);
        variables.insert("lineColor", color("muted"));
        variables.insert("textColor", color("foreground"));
        variables.insert("noteBkgColor", color("codeBackground"));
        variables.insert("noteTextColor", color("foreground"));

        // Pie slices in the theme's colours, which the base theme would mix
        // from the panels' greys instead, with the page behind their labels.
        const char *const slices[] = {"note", "tip", "warning", "important", "caution", "link"};
        for (int index = 0; index < 12; ++index)
            variables.insert(QStringLiteral("pie%1").arg(index + 1), color(slices[index % 6]));
        variables.insert("pieSectionTextColor", background);
        variables.insert("pieStrokeColor", background);
        variables.insert("pieOuterStrokeColor", color("muted"));
        variables.insert("pieTitleTextColor", color("foreground"));
        variables.insert("pieLegendTextColor", color("foreground"));
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{{"theme", theme}, {"themeVariables", variables}})
                                 .toJson(QJsonDocument::Compact));
}

QString Figures::file(const Figure &figure) const {
    return m_dir + QLatin1Char('/') + figure.key
        + (figure.kind == QLatin1String("math") ? QStringLiteral(".svg") : QStringLiteral(".png"));
}

QVariantMap Figures::picture(const QString &kind, const QString &source, bool display,
                             const QVariantMap &colors, bool darkMode) {
    Figure figure{kind, QString(), source, display, style(kind, colors, darkMode)};
    figure.key = QString::fromLatin1(
        QCryptographicHash::hash(QStringList{kind, QString::number(display), QString::number(scale(kind)),
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
                    {"width", qreal(size.width()) / scale(kind)},
                    {"height", qreal(size.height()) / scale(kind)}};
    }
    if (!canDraw(kind))
        return {{"missing", true}};
    if (m_failed.contains(figure.key)
        || QFile::exists(m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".failed")))
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
            // Not straight away: printing may be waiting on this one, and the
            // page must not be shown again under it.
            if (m_processes.isEmpty()) {
                QMetaObject::invokeMethod(this, [this]() {
                    emit drawn();
                    drawWanted();
                }, Qt::QueuedConnection);
            }
        });
        // One that cannot start, or hangs, finishes all the same.
        connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                emit process->finished(-1, QProcess::CrashExit);
        });
        QTimer::singleShot(m_timeout * figures.size(), process, [this, process]() { stop(this, process); });
    }
}

void Figures::drawWantedNow(int timeout) {
    // What is wanted now is drawn here, not left to what finishes drawing in
    // the background meanwhile.
    const QHash<QString, Figure> wanted = std::exchange(m_wanted, {});
    QList<QPointer<QProcess>> running;
    for (QProcess *process : std::as_const(m_processes))
        running.append(process);
    for (const QPointer<QProcess> &process : std::as_const(running)) {
        if (process)
            process->waitForFinished(timeout);
    }

    QHash<QString, QList<Figure>> byKind;
    for (const Figure &figure : wanted)
        byKind[figure.kind].append(figure);
    for (auto it = byKind.cbegin(); it != byKind.cend(); ++it) {
        QProcess *process = start(it.key(), it.value());
        if (!process->waitForFinished(qMax(timeout, m_timeout * int(it.value().size())))) {
            stop(this, process);
            process->waitForFinished(1000);
        }
        finish(process, it.value());
    }
}

QProcess *Figures::start(const QString &kind, const QList<Figure> &figures) {
    auto *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process->setChildProcessModifier([]() { ::setpgid(0, 0); });

    if (kind == QLatin1String("mermaid")) {
        QStringList arguments{QStringLiteral("-c"), drawMermaid, QStringLiteral("sh"), m_dir,
                              QString::number(mermaidScale)};
        for (const Figure &figure : figures) {
            QFile source(m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".mmd"));
            QFile config(m_dir + QLatin1Char('/') + figure.key + QStringLiteral(".json"));
            if (source.open(QIODevice::WriteOnly) && config.open(QIODevice::WriteOnly)) {
                source.write(figure.source.toUtf8());
                config.write(figure.style.toUtf8());
                arguments.append(figure.key);
            }
        }
        process->start(QStringLiteral("sh"), arguments);
        return process;
    }

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

// A figure the program did not draw, nor marked as failed, is not asked for
// again while Omaread runs; it shows as its source.
void Figures::finish(QProcess *process, const QList<Figure> &figures) {
    for (const Figure &figure : figures) {
        m_drawing.remove(figure.key);
        if (!QFile::exists(file(figure)))
            m_failed.insert(figure.key);
        // What a stopped diagram leaves behind.
        for (const char *suffix : {".mmd", ".json", ".err"})
            QFile::remove(m_dir + QLatin1Char('/') + figure.key + QLatin1String(suffix));
    }
    m_processes.removeOne(process);
    process->deleteLater();
}
