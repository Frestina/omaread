// Renders Omaread offscreen, to see a change without opening a window: a
// screenshot of the window, or the printed pages as a PDF. Run it through
// bin/snapshot, which builds it and sets up the offscreen screen.

#include <QApplication>
#include <QFileInfo>
#include <QPrinter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTextStream>
#include <QTimer>

#include "backend.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Settings apart from Omaread's own, so a saved text size or window
    // size does not leak into the picture.
    app.setOrganizationName(QStringLiteral("omaread-snapshot"));

    QStringList args = app.arguments().mid(1);
    bool dark = true;
    qreal scroll = 0;
    while (!args.isEmpty() && args.first().startsWith(QLatin1String("--"))) {
        const QString option = args.takeFirst();
        if (option == QLatin1String("--light"))
            dark = false;
        else if (option == QLatin1String("--dark"))
            dark = true;
        else if (option == QLatin1String("--scroll") && !args.isEmpty())
            scroll = args.takeFirst().toDouble();
    }
    if (args.size() != 2) {
        QTextStream(stderr) << "usage: snapshot [--light | --dark] [--scroll Y] FILE OUT.png|OUT.pdf\n";
        return 2;
    }
    const QUrl file = QUrl::fromLocalFile(QFileInfo(args.at(0)).absoluteFilePath());
    const QString out = args.at(1);

    Backend backend;
    backend.setDarkMode(dark);

    if (out.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        backend.open(file);
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setOutputFileName(out);
        backend.printTo(printer);
        return 0;
    }

    QQuickStyle::setStyle(QStringLiteral("Material"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    // The window is the whole offscreen screen, which bin/snapshot sizes.
    window->setGeometry(window->screen()->geometry());
    backend.setParentWindow(window);
    backend.open(file);

    // Long enough for images, local or from the web, to load and settle.
    QTimer::singleShot(2500, &app, [&]() {
        if (scroll > 0)
            QMetaObject::invokeMethod(window, "scrollTo", Q_ARG(QVariant, QVariant(scroll)));
        QTimer::singleShot(300, &app, [&]() {
            if (!window->grabWindow().save(out)) {
                QTextStream(stderr) << "snapshot: could not write " << out << "\n";
                app.exit(1);
                return;
            }
            app.quit();
        });
    });
    return app.exec();
}
