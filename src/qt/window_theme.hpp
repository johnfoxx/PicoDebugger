#pragma once

#include <QColor>
#include <functional>

class QMenu;
class QWidget;
void applyPicoTheme(QWidget& window);
void addPicoThemeMenu(QMenu& views, QWidget& window, std::function<void()> repaint);
QColor picoThemeColor(const char* darkColor);
