#pragma once
#include "picodbg/peripherals.hpp"
#include <QWidget>
#include <functional>

class QLabel;
class QTableWidget;
class QTimer;
class QPushButton;

// A presentation-only view. Values come from the parent's coherent snapshot;
// the inspector never reads MMIO itself and cannot consume a FIFO/clear an IRQ.
class RegisterInspector final : public QWidget {
public:
    explicit RegisterInspector(QWidget* parent = nullptr);
    void setRegister(const picodbg::RegisterDescription& description);
    void setValue(bool available, uint32_t value, const QString& status, bool writable);
    void clear();
    std::function<void(const picodbg::RegisterField&)> editField;
private:
    picodbg::RegisterDescription register_;
    bool selected_ = false;
    QLabel* title_;
    QTableWidget* fields_;
    QTimer* resizeRows_;
    QPushButton* edit_;
    bool available_ = false, writable_ = false;
    void updateEditor();
};
