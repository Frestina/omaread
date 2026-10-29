#include "hyprlandrounding.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

HyprlandRounding::HyprlandRounding(QObject *parent) : QObject(parent) {
    if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
        return;
    ask();
    listen();
}

int HyprlandRounding::parse(const QByteArray &json) {
    const QJsonValue value = QJsonDocument::fromJson(json).object().value(QStringLiteral("int"));
    return value.isDouble() && value.toInt() >= 0 ? value.toInt() : -1;
}

void HyprlandRounding::ask() {
    const QString hyprctl = QStandardPaths::findExecutable(QStringLiteral("hyprctl"));
    if (hyprctl.isEmpty())
        return;
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process]() {
        const int rounding = parse(process->readAllStandardOutput());
        process->deleteLater();
        if (rounding >= 0 && rounding != m_rounding) {
            m_rounding = rounding;
            emit roundingChanged(rounding);
        }
    });
    connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    process->start(hyprctl, {QStringLiteral("-j"), QStringLiteral("getoption"), QStringLiteral("decoration:rounding")});
}

// Hyprland tells its event socket's listeners a line for each event;
// configreloaded follows a theme change or a saved config.
void HyprlandRounding::listen() {
    m_events = new QLocalSocket(this);
    connect(m_events, &QLocalSocket::readyRead, this, [this]() {
        bool reloaded = false;
        while (m_events->canReadLine()) {
            if (m_events->readLine().startsWith("configreloaded>>"))
                reloaded = true;
        }
        if (reloaded)
            ask();
    });
    m_events->connectToServer(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/hypr/")
                              + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE") + QStringLiteral("/.socket2.sock"));
}
