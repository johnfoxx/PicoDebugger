#include "qt_diagnostics.hpp"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QString>
#include <QtGlobal>
#include <cstdio>
#include <cstring>
#if defined(__linux__) && defined(__GLIBC__)
#include <execinfo.h>
#endif

namespace {
QtMessageHandler previousHandler = nullptr;

void diagnosticMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    // Preserve every diagnostic, including messages outside the two issues
    // under investigation. Do not recursively log with qWarning/qDebug here.
    if (previousHandler) previousHandler(type, context, message);
    else {
        const auto formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
        std::fprintf(stderr, "%s\n", formatted.constData());
    }
    if (type != QtWarningMsg || (!message.contains("QSocketNotifier")
                                 && !message.contains("requestActivate"))) return;
    static thread_local bool tracing = false;
    if (tracing) return;
    tracing = true;
    std::fprintf(stderr, "[PicoDebugger Qt] pile d'appel du diagnostic :\n");
#if defined(__linux__) && defined(__GLIBC__)
    void* frames[32];
    const int count = backtrace(frames, 32);
    backtrace_symbols_fd(frames, count, 2);
#else
    std::fprintf(stderr, "Trace native indisponible sur cette plateforme.\n");
#endif
    tracing = false;
}
}

bool installQtDiagnostics(int argc, char* const argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--") == 0) break;
        if (std::strcmp(argv[i], "--qt-diagnostics") != 0) continue;
        std::fprintf(stderr, "[PicoDebugger Qt] Qt compilé=%s, chargé=%s ; démarrage QApplication\n", QT_VERSION_STR, qVersion());
        previousHandler = qInstallMessageHandler(diagnosticMessage);
        return true;
    }
    return false;
}

void reportQtEnvironment() {
    std::fprintf(stderr, "[PicoDebugger Qt] plateforme=%s ; saisie demandée=%s ; thème demandé=%s ; plugins=%s\n",
                 QGuiApplication::platformName().toLocal8Bit().constData(),
                 qgetenv("QT_IM_MODULE").constData(),
                 qgetenv("QT_QPA_PLATFORMTHEME").constData(),
                 QCoreApplication::libraryPaths().join(QLatin1Char(';')).toLocal8Bit().constData());
}
