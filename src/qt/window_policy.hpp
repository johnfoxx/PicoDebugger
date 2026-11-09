#pragma once
#include <QGuiApplication>
#include <QWidget>

// Qt5's Wayland backend cannot implement requestActivateWindow(). Leave
// activation/placement to the compositor instead of filtering its warnings.
inline bool waylandDesktop() { return QGuiApplication::platformName().startsWith("wayland"); }
inline void prepareInspectionWindow(QWidget* window) {
    if (waylandDesktop()) window->setAttribute(Qt::WA_ShowWithoutActivating);
}
inline void showInspectionWindow(QWidget* window) {
    prepareInspectionWindow(window);
    window->show();
    const auto platform = QGuiApplication::platformName();
    if (!waylandDesktop() && platform != "offscreen" && platform != "minimal") window->raise();
}
