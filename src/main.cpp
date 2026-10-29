#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QUrl>
#include <QWindow>

#include "backend.h"
#include "cli.h"
#include "systemtheme.h"

#define OMAREAD_VERSION "0.3.1"

int main(int argc, char *argv[]) {
    // Answered before the application exists, so they need no display.
    if (const std::optional<int> exitCode =
            Cli::handleArguments(argc, argv, QStringLiteral(OMAREAD_VERSION)))
        return *exitCode;

    // Omarchy asks Qt for GTK 3 dialogs, which run inside this process and can
    // abort it; the desktop portal shows the same file chooser from a process
    // of its own.
    if (qgetenv("QT_QPA_PLATFORMTHEME") == "gtk3")
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");

    // A QApplication rather than a QGuiApplication, for the print dialog,
    // which is a widget.
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("omaread"));
    app.setApplicationVersion(QStringLiteral(OMAREAD_VERSION));
    app.setOrganizationName(QStringLiteral("omaread"));
    app.setDesktopFileName(QStringLiteral("omaread"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("omaread")));

    QQuickStyle::setStyle(QStringLiteral("Material"));

    Backend backend(&app);
    SystemTheme systemTheme(&app);
    backend.setDarkMode(systemTheme.darkMode());
    QObject::connect(&systemTheme, &SystemTheme::darkModeChanged, &backend, &Backend::setDarkMode);
    backend.setDesktopTextScale(systemTheme.textScale());
    QObject::connect(&systemTheme, &SystemTheme::textScaleChanged, &backend,
                     &Backend::setDesktopTextScale);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [](const QList<QQmlError> &warnings) {
        for (const QQmlError &warning : warnings)
            qWarning().noquote() << warning.toString();
    });
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        qCritical() << "Could not load the Omaread interface";
        return -1;
    }
    backend.setParentWindow(qobject_cast<QWindow *>(engine.rootObjects().constFirst()));

    const QStringList args = app.arguments();
    if (args.size() > 1) {
        const QString &argument = args.at(1);
        // File managers can hand over a file:// URL instead of a path.
        backend.open(argument.startsWith(QLatin1String("file:"))
                         ? QUrl(argument)
                         : QUrl::fromLocalFile(QFileInfo(argument).absoluteFilePath()));
    }

    return app.exec();
}
