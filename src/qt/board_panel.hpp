#pragma once
#include "picodbg/board.hpp"
#include <QWidget>
class QLabel;
class QTableWidget;
namespace picodbg { class DebugTarget; }

class BoardLed : public QWidget {
public:
    explicit BoardLed(QWidget* parent = nullptr);
    void updateTarget(const picodbg::DebugTarget& target);
private:
    QLabel* lamp_;
    QLabel* label_;
};

class BoardPanel : public QWidget {
public:
    explicit BoardPanel(QWidget* parent = nullptr);
    void setBoard(const picodbg::BoardDescription& board, bool hardware = false);
private:
    QLabel* summary_;
    QTableWidget* wiring_;
};
