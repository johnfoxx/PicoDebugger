#pragma once
#include "picodbg/debugger.hpp"
#include <QWidget>
#include <functional>
class QPlainTextEdit;

// Reused by tabbed inspectors and the independent peripheral explorer.
class UartTerminal : public QWidget {
public:
    explicit UartTerminal(unsigned uart, QWidget* parent = nullptr);
    void updateTarget(const picodbg::DebugTarget& debugger);
    std::function<size_t(unsigned, const std::string&)> injectUart;
private:
    unsigned uart_;
    QPlainTextEdit* output_;
};
