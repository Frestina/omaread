#pragma once

#include <QObject>

class QLocalSocket;

// Hyprland's window corner rounding, decoration:rounding, as Omarchy's own
// shell mirrors it: Omarchy's default, a theme's or the user's own. It is
// asked for again each time Hyprland reloads its config, as it does when the
// theme changes. Without Hyprland it stays unknown.
class HyprlandRounding : public QObject {
    Q_OBJECT

public:
    explicit HyprlandRounding(QObject *parent = nullptr);

    // In pixels, or -1 while it is not known.
    int rounding() const { return m_rounding; }

    // The rounding in what `hyprctl -j getoption decoration:rounding`
    // answers, or -1.
    static int parse(const QByteArray &json);

signals:
    void roundingChanged(int rounding);

private:
    void ask();
    void listen();

    QLocalSocket *m_events = nullptr;
    int m_rounding = -1;
};
