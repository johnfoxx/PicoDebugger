#pragma once
#include <QByteArray>
#include <QtGlobal>

// Must run before QApplication: Qt5/Wayland constructs the explicitly selected
// IBus plugin before creating its event dispatcher. IBus's file watcher then
// creates an unusable socket notifier. Let QtWayland select its native input
// context (with Qt's compose fallback), only for this affected combination.
inline bool prepareQtInputMethod(int argc, char* const argv[]) {
#if defined(Q_OS_LINUX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    bool systemInput = false;
    for (int i = 1; i < argc; ++i) {
        const QByteArray argument(argv[i]);
        if (argument == "--") break;
        if (argument == "--qt-system-input") systemInput = true;
        if ((argument == "-platform" || argument == "--platform") && i + 1 < argc)
            platform = argv[++i];
        else if (argument.startsWith("-platform=")) platform = argument.mid(10);
        else if (argument.startsWith("--platform=")) platform = argument.mid(11);
    }
    const bool wayland = platform.isEmpty()
        ? (!qgetenv("WAYLAND_DISPLAY").isEmpty() || qgetenv("XDG_SESSION_TYPE") == "wayland")
        : platform.startsWith("wayland");
    if (!systemInput && wayland && qgetenv("QT_IM_MODULE") == "ibus") {
        qunsetenv("QT_IM_MODULE");
        return true;
    }
#else
    Q_UNUSED(argc);
    Q_UNUSED(argv);
#endif
    return false;
}
