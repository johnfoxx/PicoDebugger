#pragma once
#include "debug_access.hpp"
#include <QWidget>
#include <functional>
#include <map>

namespace picodbg { class DebugTarget; }
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

struct ValueWatch {
    QString device;
    uint32_t address;
    unsigned size;
    unsigned core;
    bool range;
    int color;
    ValueWatch(const QString& device, uint32_t address, unsigned size, unsigned core, bool range = false, int color = 0)
        : device(device), address(address), size(size), core(core), range(range), color(color) {}
    bool operator==(const ValueWatch& other) const;
};

class ValueWatches final : public QWidget {
public:
    explicit ValueWatches(QWidget* parent = nullptr);
    void updateTarget(const picodbg::DebugTarget& target);
    void reloadCatalog() { initialized_ = false; }
    bool contains(const ValueWatch& watch) const;
    void add(const ValueWatch& watch);
    void remove(const ValueWatch& watch);
    int count() const;
    std::vector<MemoryHighlight> memoryHighlights(const picodbg::ChipDescription& chip, unsigned core) const;
    std::function<void()> changed;
    std::function<void(ValueWatch)> open;
    std::function<void(ValueWatch)> openMemory;
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    QLabel* empty_;
    QTreeWidget* tree_;
    QPushButton* open_;
    QPushButton* memory_;
    QPushButton* remove_;
    bool initialized_ = false;
    unsigned nextColor_ = 0;
    picodbg::Chip chip_ = picodbg::Chip::RP2040;
    std::map<picodbg::Chip, std::vector<ValueWatch>> watches_;
    std::vector<picodbg::PeripheralDescription> devices_;
    QTreeWidgetItem* makeItem(const ValueWatch& watch);
    void refreshActions();
};
