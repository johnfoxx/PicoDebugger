#pragma once
#include "picodbg/board.hpp"
#include <QWidget>
class QLabel;
class QTableWidget;

class BoardPanel : public QWidget {
public:
    explicit BoardPanel(QWidget* parent = nullptr);
    void setBoard(const picodbg::BoardDescription& board, bool hardware = false);
private:
    QLabel* summary_;
    QTableWidget* wiring_;
};
