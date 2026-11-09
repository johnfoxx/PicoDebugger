#pragma once
#include "picodbg/debugger.hpp"
#include "picodbg/peripherals.hpp"
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QTableWidget;
class QTreeWidget;
class RegisterInspector;

class PeripheralPanel : public QWidget {
public:
    explicit PeripheralPanel(QWidget* parent = nullptr, const QString& pinnedDevice = {});
    void updateTarget(const picodbg::Debugger& debugger, unsigned core);
    std::function<void(const QString&)> openDevice;
    QString selectedDevice() const;
private:
    QString pinnedDevice_;
    const picodbg::Debugger* debugger_ = nullptr;
    picodbg::Chip chip_ = picodbg::Chip::RP2040;
    unsigned core_ = 0;
    int selected_ = -1;
    std::vector<picodbg::PeripheralDescription> devices_;
    QLineEdit* filter_;
    QLineEdit* registerFilter_;
    QLabel* context_;
    QLabel* detail_;
    QTreeWidget* catalog_;
    QTableWidget* registers_;
    RegisterInspector* inspector_;
    void rebuildCatalog();
    void applyFilter();
    void selectDevice();
    void refreshValues();
    void filterRegisters();
    void refreshRegisterDetails();
};
