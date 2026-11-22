#pragma once
#include "picodbg/target.hpp"
#include "picodbg/peripherals.hpp"
#include <QWidget>
#include <functional>

class QLabel;
class QAction;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class RegisterInspector;

class PeripheralPanel : public QWidget {
public:
    explicit PeripheralPanel(QWidget* parent = nullptr, const QString& pinnedDevice = {}, bool expanded = false);
    void updateTarget(const picodbg::DebugTarget& debugger, unsigned core, bool running = false);
    void focusDevice(const QString& name); // Empty name restores the catalogue.
    void selectRegister(uint32_t address);
    std::function<void(const QString&)> openDevice;
    std::function<void(const QString&)> selectionChanged;
    std::function<void(uint32_t, unsigned)> openMemory;
    std::function<bool(const QString&, uint32_t, unsigned, unsigned)> isWatched;
    std::function<void(const QString&, uint32_t, unsigned, unsigned)> toggleWatch;
    std::function<void(const QString&, const picodbg::RegisterDescription&, unsigned, const QString&)> editRegister;
    QString selectedDevice() const;
protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QString pinnedDevice_;
    const picodbg::DebugTarget* debugger_ = nullptr;
    picodbg::Chip chip_ = picodbg::Chip::RP2040;
    unsigned core_ = 0;
    int selected_ = -1;
    std::vector<picodbg::PeripheralDescription> devices_;
    QLineEdit* filter_;
    QLineEdit* registerFilter_;
    QLabel* context_;
    QLabel* detail_;
    QLabel* empty_;
    QLabel* registerEmpty_;
    QPushButton* open_;
    QAction* readOnce_;
    QPushButton* watchDevice_;
    QPushButton* memory_;
    QAction* watchRegister_;
    QAction* writeRegister_;
    QPushButton* bits_;
    bool hardware_ = false;
    bool running_ = false;
    QTreeWidget* catalog_;
    QTableWidget* registers_;
    RegisterInspector* inspector_;
    void rebuildCatalog();
    void refreshCatalogColors();
    void applyFilter();
    void selectDevice();
    const picodbg::RegisterDescription* selectedRegister() const;
    void updateHeading();
    void refreshValues();
    void filterRegisters();
    void refreshRegisterDetails();
};
