#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

class QProcess;

// Math and Mermaid diagrams, drawn by programs of their own into pictures in
// the cache folder: MathJax under node, GitHub's math engine, and Mermaid's
// own mmdc. Each is drawn once in each set of colours and kept.
class Figures : public QObject {
    Q_OBJECT

public:
    explicit Figures(QObject *parent = nullptr);
    ~Figures() override;

    // Whether what draws a kind of figure, "math" or "mermaid", is installed.
    static bool canDraw(const QString &kind);

    // The colours a figure is drawn in, as its program takes them: a colour
    // for a formula, Mermaid's configuration for a diagram.
    static QString style(const QString &kind, const QVariantMap &colors, bool darkMode);

    // The picture of a figure in the page's colours, as {src, width, height},
    // its size in CSS pixels at 16px text. Empty while it is not drawn yet,
    // and then it is wanted, or when it cannot be drawn; {missing: true} when
    // what draws it is not installed.
    QVariantMap picture(const QString &kind, const QString &source, bool display,
                        const QVariantMap &colors, bool darkMode);

    // Draws what is wanted in the background, and says drawn() when done.
    void drawWanted();
    // Draws what is wanted and waits for it, for printing.
    void drawWantedNow(int timeout);
    bool drawing() const { return !m_processes.isEmpty(); }

signals:
    void drawn();

private:
    friend class OmareadTest;

    struct Figure {
        QString kind;
        QString key;
        QString source;
        bool display = false;
        QString style;
    };

    QString file(const Figure &figure) const;
    QProcess *start(const QString &kind, const QList<Figure> &figures);
    void finish(QProcess *process, const QList<Figure> &figures);

    QString m_dir;
    // How long a program is given for each figure before it is given up on.
    int m_timeout = 20000;
    QHash<QString, Figure> m_wanted;
    QSet<QString> m_drawing;
    // What was not drawn for a reason that may pass, such as a program that
    // timed out or would not start, is not asked for again until Omaread
    // starts again; what a program could not draw it marks .failed for good.
    QSet<QString> m_failed;
    QList<QProcess *> m_processes;
};
