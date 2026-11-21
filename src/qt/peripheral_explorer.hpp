#pragma once
#include "picodbg/debugger.hpp"
#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QTabWidget;
class PeripheralPanel;
class UartTerminal;

// A separate workspace: changing its view or core never replaces the assembly
// page or changes the main debugger's selected CPU context.
class PeripheralExplorer : public QDialog {
public:
    PeripheralExplorer(const picodbg::DebugTarget& debugger, unsigned core, QWidget* parent = nullptr);
    void updateTarget(const picodbg::DebugTarget& debugger);
    std::function<void()> refreshRequested;
    std::function<size_t(unsigned, const std::string&)> injectUart;
private:
    QComboBox* view_;
    QComboBox* core_;
    QLabel* snapshot_;
    QTabWidget* tabs_;
    PeripheralPanel* panel_;
    UartTerminal* terminals_[2];
};
