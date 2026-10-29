#include "backend.h"

#include "markdownrenderer.h"
#include "printdocument.h"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPageLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTextStream>
#include <QWindow>

namespace {
// Browser zoom steps, so Ctrl+= and Ctrl+- feel as they do on github.com.
constexpr qreal textZoomSteps[] = {0.5, 0.67, 0.75, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
constexpr int textZoomStepCount = int(sizeof(textZoomSteps) / sizeof(textZoomSteps[0]));
const QString textZoomSetting = QStringLiteral("textZoom");

const QStringList markdownSuffixes{QStringLiteral("md"), QStringLiteral("markdown"),
                                   QStringLiteral("mdown"), QStringLiteral("mkd"),
                                   QStringLiteral("mkdn"), QStringLiteral("mdwn")};

QString themeColorsPath() {
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
}

QColor blend(const QColor &from, const QColor &to, qreal amount) {
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
                            from.greenF() + (to.greenF() - from.greenF()) * amount,
                            from.blueF() + (to.blueF() - from.blueF()) * amount);
}

// A TOML string value without its quotes, or a bare value as it is.
QString tomlValue(const QString &text) {
    const QString value = text.trimmed();
    if (value.isEmpty())
        return value;
    const QChar quote = value.front();
    if (quote != QLatin1Char('"') && quote != QLatin1Char('\''))
        return value;
    const int close = value.indexOf(quote, 1);
    return close < 0 ? value : value.mid(1, close - 1);
}

// An image for the printed page, and the size it shows at. An SVG is drawn
// at four times that, so badges and logos stay sharp on paper.
struct PrintImage {
    QImage image;
    QSize size;
};

PrintImage decodeImage(const QByteArray &data) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    if (!reader.canRead())
        return {};
    const QSize size = reader.size();
    if (reader.format().startsWith("svg") && size.isValid())
        reader.setScaledSize(size * 4);
    const QImage image = reader.read();
    return {image, size.isValid() ? size : image.size()};
}

// Every image the page names, from disk or the web. A web image that takes
// longer than ten seconds is left out rather than holding up the print.
QHash<QString, PrintImage> loadPrintImages(const QStringList &sources) {
    QHash<QString, PrintImage> images;
    QNetworkAccessManager network;
    QList<QPair<QString, QNetworkReply *>> downloads;
    for (const QString &source : sources) {
        const QUrl url(source);
        if (url.isLocalFile()) {
            QFile file(url.toLocalFile());
            if (file.open(QIODevice::ReadOnly))
                images.insert(source, decodeImage(file.readAll()));
        } else if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https")) {
            downloads.append({source, network.get(QNetworkRequest(url))});
        }
    }

    if (!downloads.isEmpty()) {
        QEventLoop loop;
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        qsizetype remaining = downloads.size();
        for (const auto &[source, reply] : std::as_const(downloads)) {
            QObject::connect(reply, &QNetworkReply::finished, &loop, [&remaining, &loop]() {
                if (--remaining == 0)
                    loop.quit();
            });
        }
        loop.exec();
        for (const auto &[source, reply] : std::as_const(downloads)) {
            if (reply->isFinished() && reply->error() == QNetworkReply::NoError)
                images.insert(source, decodeImage(reply->readAll()));
            reply->abort();
            reply->deleteLater();
        }
    }

    images.removeIf([](const auto &entry) { return entry.value().image.isNull(); });
    return images;
}

// The folder holding .git above path, where GitHub roots links that start
// with '/'.
QUrl repositoryRoot(const QString &directory) {
    QDir dir(directory);
    do {
        if (dir.exists(QStringLiteral(".git")))
            return QUrl::fromLocalFile(dir.absolutePath() + QLatin1Char('/'));
    } while (dir.cdUp());
    return {};
}
}

Backend::Backend(QObject *parent) : QObject(parent) {
    m_textZoom = qBound(textZoomSteps[0], QSettings().value(textZoomSetting, 1.0).toReal(),
                        textZoomSteps[textZoomStepCount - 1]);

    // GitHub sets text in the system's sans, Noto Sans on Linux, and code in
    // its monospace.
    m_textFont = QFontDatabase::families().contains(QStringLiteral("Noto Sans"))
        ? QStringLiteral("Noto Sans")
        : QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    m_codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    m_canEdit = !QStandardPaths::findExecutable(QStringLiteral("omawrite")).isEmpty();

    // Editors save by replacing the file, which shows up as a burst of
    // changes, and the file briefly missing; let it settle first.
    m_fileCheckTimer.setSingleShot(true);
    m_fileCheckTimer.setInterval(100);
    connect(&m_fileCheckTimer, &QTimer::timeout, this, &Backend::checkFile);
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, &m_fileCheckTimer,
            qOverload<>(&QTimer::start));
    connect(&m_fileWatcher, &QFileSystemWatcher::directoryChanged, &m_fileCheckTimer,
            qOverload<>(&QTimer::start));

    loadTheme();
    watchTheme();
    m_themeReloadTimer.setSingleShot(true);
    m_themeReloadTimer.setInterval(100);
    connect(&m_themeReloadTimer, &QTimer::timeout, this, [this]() {
        loadTheme();
        watchTheme();
    });
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, &m_themeReloadTimer,
            qOverload<>(&QTimer::start));
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, &m_themeReloadTimer,
            qOverload<>(&QTimer::start));
}

QString Backend::fileName() const {
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Backend::displayPath() const {
    const QString home = QDir::homePath();
    if (m_path.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + m_path.mid(home.size());
    return m_path;
}

bool Backend::isMarkdown(const QString &path) {
    return markdownSuffixes.contains(QFileInfo(path).suffix().toLower());
}

void Backend::open(const QUrl &url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty())
        return;
    m_history.clear();
    show(QFileInfo(path).absoluteFilePath(), url.fragment(), 0);
}

void Backend::show(const QString &path, const QString &anchor, qreal position) {
    load(path, false);
    emit documentLoaded(anchor, position);
}

bool Backend::load(const QString &path, bool reloading) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (reloading) {
            setStatus(QStringLiteral("Could not read the file again"));
            return false;
        }
        m_path = path;
        m_contents.clear();
        m_blocks.clear();
        m_anchors.clear();
        m_error = QFileInfo::exists(path)
            ? QStringLiteral("Could not read %1").arg(QFileInfo(path).fileName())
            : QStringLiteral("%1 does not exist").arg(QFileInfo(path).fileName());
        watchFile();
        emit documentChanged();
        return false;
    }

    const QByteArray contents = file.readAll();
    if (reloading && contents == m_contents)
        return false;

    const QFileInfo info(path);
    m_path = info.absoluteFilePath();
    m_contents = contents;
    m_rootUrl = repositoryRoot(info.absolutePath());
    const RenderedMarkdown rendered = MarkdownRenderer::render(contents, baseUrl(), m_rootUrl);
    m_blocks = rendered.blocks;
    m_anchors = rendered.anchors;
    m_error.clear();
    m_fileMissing = false;
    watchFile();

    if (reloading)
        emit aboutToReload();
    emit documentChanged();
    return true;
}

void Backend::reload() {
    if (m_path.isEmpty())
        return;
    if (load(m_path, true))
        emit documentLoaded(QString(), -1);
    setStatus(QStringLiteral("Reloaded"));
}

QUrl Backend::baseUrl() const {
    return QUrl::fromLocalFile(QFileInfo(m_path).absolutePath() + QLatin1Char('/'));
}

void Backend::activateLink(const QString &href, qreal position) {
    if (href.startsWith(QLatin1Char('#'))) {
        emit anchorRequested(QUrl::fromPercentEncoding(href.mid(1).toUtf8()));
        return;
    }

    const QUrl url = MarkdownRenderer::resolveUrl(href, baseUrl(), m_rootUrl);
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists()) {
            setStatus(QStringLiteral("%1 does not exist").arg(info.fileName()));
            return;
        }
        // Another Markdown file opens here, as it would on GitHub; Alt+Left
        // comes back.
        if (info.isFile() && isMarkdown(path)) {
            m_history.append({m_path, position});
            show(info.absoluteFilePath(), url.fragment(QUrl::FullyDecoded), 0);
            return;
        }
    }
    QDesktopServices::openUrl(url);
}

void Backend::goBack() {
    if (m_history.isEmpty())
        return;
    const auto [path, position] = m_history.takeLast();
    show(path, QString(), position);
}

int Backend::anchorIndex(const QString &anchor) const {
    if (m_anchors.contains(anchor))
        return m_anchors.value(anchor);
    // Links written by hand often keep the heading's capitals.
    return m_anchors.value(MarkdownRenderer::slugify(anchor), -1);
}

void Backend::edit() {
    if (m_path.isEmpty() || !m_canEdit)
        return;
    QProcess::startDetached(QStringLiteral("omawrite"), {m_path});
    setStatus(QStringLiteral("Opened in Omawrite"));
}

void Backend::copyText(const QString &text) {
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    setStatus(QStringLiteral("Copied"));
}

void Backend::setParentWindow(QWindow *window) {
    m_parentWindow = window;
}

void Backend::printDocument() {
    if (m_blocks.isEmpty()) {
        setStatus(QStringLiteral("There is nothing to print"));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(fileName());
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("Print %1").arg(fileName()));
    dialog.winId();
    if (dialog.windowHandle() && m_parentWindow)
        dialog.windowHandle()->setTransientParent(m_parentWindow);
    if (dialog.exec() != QDialog::Accepted)
        return;

    printTo(printer);
    setStatus(printer.outputFormat() == QPrinter::PdfFormat
                  ? QStringLiteral("Saved %1").arg(QFileInfo(printer.outputFileName()).fileName())
                  : QStringLiteral("Sent %1 to %2").arg(fileName(), printer.printerName()));
}

void Backend::printTo(QPrinter &printer) {
    QTextDocument document;
    QHash<QString, QSize> sizes;
    const QHash<QString, PrintImage> images = loadPrintImages(PrintDocument::imageSources(m_blocks));
    for (auto it = images.cbegin(); it != images.cend(); ++it) {
        document.addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value().image);
        sizes.insert(it.key(), it.value().size);
    }

    // QTextDocument::print lays the page out in 96 dpi pixels, inside a 2 cm
    // margin on the paper.
    const qreal margin = 2 / 2.54 * 96;
    const int width = int(printer.pageLayout().fullRect(QPageLayout::Inch).width() * 96 - 2 * margin);
    QFont font(m_textFont);
    font.setPointSizeF(11);
    document.setDefaultFont(font);
    document.setDefaultStyleSheet(PrintDocument::styleSheet(m_codeFont));
    document.setHtml(PrintDocument::html(m_blocks, sizes, width));
    document.print(&printer);
}

void Backend::setStatus(const QString &status) {
    // Said again even when unchanged, so a repeated action still shows.
    m_status = status;
    emit statusChanged();
}

void Backend::watchFile() {
    const QStringList watched = m_fileWatcher.files() + m_fileWatcher.directories();
    if (!watched.isEmpty())
        m_fileWatcher.removePaths(watched);
    if (m_path.isEmpty())
        return;
    // The folder too: a file replaced on save is a new file the old watch
    // does not see.
    const QFileInfo info(m_path);
    if (info.exists())
        m_fileWatcher.addPath(m_path);
    if (QFileInfo::exists(info.absolutePath()))
        m_fileWatcher.addPath(info.absolutePath());
}

void Backend::checkFile() {
    if (m_path.isEmpty())
        return;
    if (!QFileInfo::exists(m_path)) {
        if (!m_fileMissing)
            setStatus(QStringLiteral("%1 was removed").arg(fileName()));
        m_fileMissing = true;
        return;
    }
    if (m_fileMissing) {
        m_fileMissing = false;
        m_contents.clear();
    }
    if (load(m_path, true))
        emit documentLoaded(QString(), -1);
    else
        watchFile();
}

void Backend::setDarkMode(bool darkMode) {
    m_systemDarkMode = darkMode;
    loadTheme();
}

QHash<QString, QString> Backend::readTheme(const QString &path) {
    QHash<QString, QString> theme;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return theme;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        theme.insert(line.left(equals).trimmed(), tomlValue(line.mid(equals + 1)));
    }
    return theme;
}

QVariantMap Backend::themeColors(const QHash<QString, QString> &theme, bool systemDarkMode,
                                 bool *darkMode) {
    const QString mode = theme.value(QStringLiteral("mode"));
    const QColor themeBackground(theme.value(QStringLiteral("background")));
    bool dark = systemDarkMode;
    if (mode == QLatin1String("dark")) {
        dark = true;
    } else if (mode == QLatin1String("light")) {
        dark = false;
    } else if (themeBackground.isValid()) {
        dark = 0.299 * themeBackground.redF() + 0.587 * themeBackground.greenF()
                   + 0.114 * themeBackground.blueF()
            < 0.5;
    }
    if (darkMode)
        *darkMode = dark;

    // GitHub's own palette, for whatever the theme does not name.
    QVariantMap colors = dark
        ? QVariantMap{{"background", "#0d1117"}, {"foreground", "#f0f6fc"}, {"muted", "#9198a1"},
                      {"link", "#4493f8"}, {"border", "#3d444d"}, {"codeBackground", "#151b23"},
                      {"stripe", "#151b23"}, {"selection", "#1f4a7a"}, {"selectionForeground", "#f0f6fc"},
                      {"note", "#4493f8"}, {"tip", "#3fb950"}, {"important", "#ab7df8"},
                      {"warning", "#d29922"}, {"caution", "#f85149"}}
        : QVariantMap{{"background", "#ffffff"}, {"foreground", "#1f2328"}, {"muted", "#59636e"},
                      {"link", "#0969da"}, {"border", "#d1d9e0"}, {"codeBackground", "#f6f8fa"},
                      {"stripe", "#f6f8fa"}, {"selection", "#b6d6fa"}, {"selectionForeground", "#1f2328"},
                      {"note", "#0969da"}, {"tip", "#1a7f37"}, {"important", "#8250df"},
                      {"warning", "#9a6700"}, {"caution", "#d1242f"}};

    // Later calls win, so each fallback goes in before what it falls back from.
    const auto use = [&colors, &theme](const char *key, const char *themeKey) {
        const QColor color(theme.value(QLatin1String(themeKey)));
        if (color.isValid())
            colors[QLatin1String(key)] = color.name();
    };

    const QColor foreground(theme.value(QStringLiteral("foreground")));
    if (themeBackground.isValid() && foreground.isValid()) {
        // Shades GitHub mixes for itself, mixed from the theme's page and text
        // instead, so they sit right on any theme.
        colors["muted"] = blend(themeBackground, foreground, 0.62).name();
        colors["border"] = blend(themeBackground, foreground, 0.2).name();
        colors["codeBackground"] = blend(themeBackground, foreground, 0.06).name();
        colors["stripe"] = blend(themeBackground, foreground, 0.035).name();
        colors["selectionForeground"] = foreground.name();
    }
    use("background", "background");
    use("foreground", "foreground");
    use("muted", "muted");
    use("link", "accent");
    // Omarchy themes name a lighter background for panels like code's, though
    // a few set it to the page background, which would leave no panel at all.
    const QColor lighter(theme.value(QStringLiteral("lighter_background")));
    if (lighter.isValid() && lighter != themeBackground)
        colors["codeBackground"] = lighter.name();
    use("selection", "selection");
    use("selection", "selection_background");
    use("selectionForeground", "selection_foreground");
    use("note", "blue");
    use("tip", "green");
    use("important", "magenta");
    use("warning", "yellow");
    use("caution", "red");
    return colors;
}

void Backend::loadTheme() {
    bool dark = m_systemDarkMode;
    const QVariantMap colors = themeColors(readTheme(themeColorsPath()), m_systemDarkMode, &dark);
    if (colors == m_colors && dark == m_darkMode)
        return;
    m_colors = colors;
    m_darkMode = dark;
    emit colorsChanged();
}

void Backend::watchTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);

    const QString currentDir = QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
    const QString themeDir = currentDir + QStringLiteral("/theme");
    if (QDir(currentDir).exists())
        m_themeWatcher.addPath(currentDir);
    if (QDir(themeDir).exists())
        m_themeWatcher.addPath(themeDir);
    if (QFile::exists(themeColorsPath()))
        m_themeWatcher.addPath(themeColorsPath());
}

void Backend::setDesktopTextScale(qreal textScale) {
    if (qFuzzyCompare(m_desktopTextScale, textScale))
        return;
    m_desktopTextScale = textScale;
    emit textScaleChanged();
}

void Backend::increaseTextSize() {
    setTextZoom(steppedTextZoom(1));
}

void Backend::decreaseTextSize() {
    setTextZoom(steppedTextZoom(-1));
}

void Backend::resetTextSize() {
    setTextZoom(1.0);
}

// The neighbouring step from whichever step the zoom is closest to; the ends
// of the ladder hold.
qreal Backend::steppedTextZoom(int direction) const {
    int closest = 0;
    for (int step = 1; step < textZoomStepCount; ++step) {
        if (qAbs(textZoomSteps[step] - m_textZoom) < qAbs(textZoomSteps[closest] - m_textZoom))
            closest = step;
    }
    return textZoomSteps[qBound(0, closest + direction, textZoomStepCount - 1)];
}

void Backend::setTextZoom(qreal zoom) {
    setStatus(QStringLiteral("Text size %1%").arg(qRound(zoom * 100)));
    if (qFuzzyCompare(m_textZoom, zoom))
        return;
    m_textZoom = zoom;
    QSettings().setValue(textZoomSetting, zoom);
    emit textScaleChanged();
}

QVariantMap Backend::windowGeometry() const {
    QSettings settings;
    return {{"width", settings.value(QStringLiteral("window/width"), 1100)},
            {"height", settings.value(QStringLiteral("window/height"), 900)},
            {"maximized", settings.value(QStringLiteral("window/maximized"), false)}};
}

void Backend::saveWindowGeometry(int width, int height, bool maximized) {
    QSettings settings;
    if (!maximized) {
        settings.setValue(QStringLiteral("window/width"), width);
        settings.setValue(QStringLiteral("window/height"), height);
    }
    settings.setValue(QStringLiteral("window/maximized"), maximized);
}
