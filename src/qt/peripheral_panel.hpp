#pragma once
#include "picodbg/debugger.hpp"
#include "picodbg/peripherals.hpp"
#include <QWidget>
#include <functional>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class RegisterInspector;

class PeripheralPanel : public QWidget {
public:
    explicit PeripheralPanel(QWidget* parent = nullptr, const QString& pinnedDevice = {}, bool expanded = false);
    void updateTarget(const picodbg::Debugger& debugger, unsigned core);
    void focusDevice(const QString& name); // Empty name restores the catalogue.
    std::function<void(const QString&)> openDevice;
    std::function<void(const QString&)> selectionChanged;
    std::function<void()> openExplorer;
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
    QLabel* empty_;
    QPushButton* open_;
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
