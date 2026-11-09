#pragma once
#include "picodbg/peripherals.hpp"
#include <QWidget>

class QLabel;
class QTableWidget;

// A presentation-only view. Values come from the parent's coherent snapshot;
// the inspector never reads MMIO itself and cannot consume a FIFO/clear an IRQ.
class RegisterInspector final : public QWidget {
public:
    explicit RegisterInspector(QWidget* parent = nullptr);
    void setRegister(const picodbg::RegisterDescription& description);
    void setValue(bool available, uint32_t value, const QString& status);
    void clear();
private:
    picodbg::RegisterDescription register_;
    bool selected_ = false;
    QLabel* title_;
    QLabel* note_;
    QTableWidget* fields_;
};
