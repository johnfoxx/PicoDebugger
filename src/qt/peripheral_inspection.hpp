#pragma once
#include "picodbg/debugger.hpp"
#include <QWidget>
#include <functional>

class QComboBox;
class UartTerminal;
class QPushButton;
class PeripheralPanel;

// The same content is used inside a tab and inside a detached inspector.
class PeripheralInspection : public QWidget {
public:
    PeripheralInspection(const QString& name, unsigned core, QWidget* parent = nullptr);
    void updateTarget(const picodbg::DebugTarget& debugger, bool running);
    PeripheralPanel& panel() const { return *panel_; }
    void selectRegister(uint32_t address);
    void setDetached(bool detached);
    unsigned core() const;
    std::function<void()> refreshRequested;
    std::function<void()> detachRequested;
    std::function<void()> duplicateRequested;
    std::function<size_t(unsigned, const std::string&)> injectUart;
private:
    QComboBox* core_;
    PeripheralPanel* panel_;
    QPushButton* detach_;
    UartTerminal* terminal_ = nullptr;
    int uart_ = -1;
};
