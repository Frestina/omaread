#pragma once

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "figures.h"
#include "markdownrenderer.h"

class QPrinter;
class QWindow;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged)
    Q_PROPERTY(QString displayPath READ displayPath NOTIFY documentChanged)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY documentChanged)
    Q_PROPERTY(QString error READ error NOTIFY documentChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY documentChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY colorsChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY colorsChanged)
    Q_PROPERTY(qreal textScale READ textScale NOTIFY textScaleChanged)
    Q_PROPERTY(QString textFont READ textFont CONSTANT)
    Q_PROPERTY(QString codeFont READ codeFont CONSTANT)
    Q_PROPERTY(bool canEdit READ canEdit CONSTANT)

public:
    explicit Backend(QObject *parent = nullptr);

    QString fileName() const;
    QString filePath() const { return m_path; }
    // The path with the home folder shortened to ~, as a shell prompt shows it.
    QString displayPath() const;
    QVariantList blocks() const { return m_blocks; }
    QString error() const { return m_error; }
    bool canGoBack() const { return !m_history.isEmpty(); }
    QString status() const { return m_status; }
    bool darkMode() const { return m_darkMode; }
    QVariantMap colors() const { return m_colors; }
    // What the desktop asks for, times what the user asked for on top of it.
    qreal textScale() const { return m_desktopTextScale * m_textZoom; }
    QString textFont() const { return m_textFont; }
    QString codeFont() const { return m_codeFont; }
    bool canEdit() const { return m_canEdit; }

    void setDarkMode(bool darkMode);
    void setDesktopTextScale(qreal textScale);
    void setParentWindow(QWindow *window);
    // Lays the document out for paper and prints it, without asking.
    void printTo(QPrinter &printer);
    // Whether formulas or diagrams are still being drawn.
    bool drawingFigures() const { return m_figures.drawing(); }

    static bool isMarkdown(const QString &path);
    static QHash<QString, QString> readTheme(const QString &path);
    // The page's colours: GitHub's, with the Omarchy theme's laid over them.
    static QVariantMap themeColors(const QHash<QString, QString> &theme, bool systemDarkMode,
                                   bool *darkMode);

    Q_INVOKABLE void open(const QUrl &url);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void activateLink(const QString &href, qreal position);
    Q_INVOKABLE void goBack();
    Q_INVOKABLE int anchorIndex(const QString &anchor) const;
    // Where the block at index before the last reload is now, as {index,
    // same}; see MarkdownRenderer::followBlock.
    Q_INVOKABLE QVariantMap followBlock(int index) const;
    Q_INVOKABLE void edit();
    Q_INVOKABLE void copyText(const QString &text);
    // Marks find in page's matches in one text's document, a TextEdit's
    // textDocument, and says where they start; current is the start of the
    // one to show as current, or -1.
    Q_INVOKABLE QVariantList find(QObject *textDocument, const QString &query, int current) const;
    Q_INVOKABLE void printDocument();
    // Where Save as PDF suggests the PDF goes: beside the file, named after it.
    Q_INVOKABLE QUrl pdfUrl() const;
    // Prints the document to a PDF at url, as printing to a file would.
    Q_INVOKABLE void savePdf(const QUrl &url);
    Q_INVOKABLE void increaseTextSize();
    Q_INVOKABLE void decreaseTextSize();
    Q_INVOKABLE void resetTextSize();
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(int width, int height, bool maximized);

signals:
    void documentChanged();
    void statusChanged();
    void colorsChanged();
    void textScaleChanged();
    // The file changed on disk and is about to be shown again; the view
    // keeps its place.
    void aboutToReload();
    // A document is showing. The view goes to anchor if there is one, else to
    // position; a negative position keeps the place it had.
    void documentLoaded(const QString &anchor, qreal position);
    void anchorRequested(const QString &anchor);

private:
    friend class OmareadTest;

    bool load(const QString &path, bool reloading);
    void show(const QString &path, const QString &anchor, qreal position);
    void render();
    // Shows the file again where it is, for new pictures of its figures.
    void rerender();
    MarkdownRenderer::FigureLookup figureLookup(const QVariantMap &colors, bool darkMode);
    void setStatus(const QString &status);
    void setTextZoom(qreal zoom);
    qreal steppedTextZoom(int direction) const;
    void watchFile();
    void checkFile();
    void loadTheme();
    void watchTheme();
    QUrl baseUrl() const;

    QPointer<QWindow> m_parentWindow;
    QString m_path;
    QUrl m_rootUrl;
    QByteArray m_contents;
    QVariantList m_blocks;
    // The blocks before the last reload, to find the reader's place in.
    QVariantList m_previousBlocks;
    QHash<QString, int> m_anchors;
    int m_figureCount = 0;
    Figures m_figures;
    QString m_error;
    QList<QPair<QString, qreal>> m_history;
    QString m_status;
    bool m_fileMissing = false;
    QFileSystemWatcher m_fileWatcher;
    QTimer m_fileCheckTimer;

    bool m_darkMode = true;
    bool m_systemDarkMode = true;
    QVariantMap m_colors;
    QFileSystemWatcher m_themeWatcher;
    QTimer m_themeReloadTimer;

    qreal m_desktopTextScale = 1.0;
    qreal m_textZoom = 1.0;
    QString m_textFont;
    QString m_codeFont;
    bool m_canEdit = false;
};
