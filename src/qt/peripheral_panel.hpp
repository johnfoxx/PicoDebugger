#pragma once
#include "picodbg/target.hpp"
#include "picodbg/peripherals.hpp"
#include <QColor>
#include <QVariantMap>
#include <QWidget>
#include <functional>
#include <memory>

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
    void reloadCatalog() { debugger_ = nullptr; }
    QVariantMap saveSelection() const;
    void restoreSelection(const QVariantMap& selection);
    std::function<void(const QString&)> openDevice;
    std::function<void(const QString&)> selectionChanged;
    std::function<void(uint32_t, unsigned)> openMemory;
    std::function<bool(const QString&, uint32_t, unsigned, unsigned)> isWatched;
    std::function<void(const QString&, uint32_t, unsigned, unsigned)> toggleWatch;
    std::function<void(const QString&, const picodbg::RegisterDescription&, unsigned, const QString&)> editRegister;
    std::function<void(const picodbg::RegisterDescription&, const picodbg::RegisterField&, unsigned, uint32_t)> editField;
    QString selectedDevice() const;
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    QString pinnedDevice_;
    const picodbg::DebugTarget* debugger_ = nullptr;
    unsigned core_ = 0;
    int selected_ = -1;
    std::shared_ptr<const std::vector<picodbg::PeripheralDescription>> devices_;
    std::vector<QString> deviceSearch_, registerSearch_;
    QVariantMap pendingSelection_;
    QColor valueColor_;
    uint64_t revision_ = 0;
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
    void refreshRegisterColors();
    void applyFilter();
    void selectDevice(int index);
    void applySavedSelection();
    void selectRegisterRow(uint32_t address);
    const picodbg::RegisterDescription* selectedRegister() const;
    void updateHeading();
    void refreshValues();
    void notifyRegisterCells(int firstColumn, int lastColumn);
    void filterRegisters();
    void refreshRegisterDetails();
};
