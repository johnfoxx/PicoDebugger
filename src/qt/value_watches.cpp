#include "value_watches.hpp"
#include "debug_access.hpp"
#include "debug_value.hpp"
#include "window_theme.hpp"
#include <QHeaderView>
#include <QLabel>
#include <QResizeEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString q(const std::string& text) { return QString::fromStdString(text); }

const char* watchColors[] = {"#285168", "#554477", "#365740", "#695028", "#673e53", "#285a56"};
constexpr unsigned watchColorCount = sizeof(watchColors) / sizeof(*watchColors);
constexpr unsigned rangeRowSize = 4;
QColor watchColor(const ValueWatch& watch) { return picoThemeColor(watchColors[watch.color]); }

class WatchItem final : public QTreeWidgetItem {
public:
    WatchItem(const ValueWatch& watch, const picodbg::RegisterDescription* reg, bool emulated,
              DebugAccess access = DebugAccess::Unknown)
        : watch(watch), reg(reg), emulated(emulated), access_(reg ? registerAccess(*reg) : access) {}

    void refresh(const picodbg::DebugTarget& target) {
        const auto access = watch.device.isEmpty() ? memoryAccess(target.description(), watch.address, watch.size) : access_;
        setIcon(0, debugColorIcon(watchColor(watch)));
        setForeground(0, debugAccessColor(access));
        setBackground(0, watchColor(watch));
        const auto address = reg ? reg->address : watch.address;
        const auto detail = watch.device.isEmpty()
            ? QString(" · %1 %2").arg(watch.range ? watch.size : 8 * watch.size).arg(watch.range ? "octets" : "bits")
            : QString();
        const auto where = watch.device.isEmpty() ? detail : watch.size ? " · " + q(picodbg::hex(address)) : QString();
        setToolTip(0, QString("%1 · cœur %2%3\n%4").arg(text(0)).arg(watch.core).arg(where,
            reg ? registerAccessText(*reg) : debugAccessText(access)));
        if (childCount()) {
            if (isExpanded()) {
                for (int i = 0; i < childCount(); ++i) static_cast<WatchItem*>(child(i))->refresh(target);
            }
            return;
        }
        if (watch.range) {
            if (!target.loaded() || target.busy() || target.executing()) {
                const auto status = inspectValue(target, watch.address, 1, watch.core);
                setText(1, status.text);
                setToolTip(1, status.detail);
                return;
            }
            QStringList bytes;
            for (unsigned i = 0; i < watch.size; ++i) {
                try { bytes << q(picodbg::hex(peekMemoryByte(target, watch.address + i, watch.core), 2).substr(2)).toUpper(); }
                catch (const picodbg::Error&) { bytes << "??"; }
            }
            const auto text = bytes.join(QLatin1Char(' '));
            setText(1, text);
            setToolTip(1, text);
            return;
        }
        const auto value = reg ? inspectRegister(target, *reg, watch.core, emulated)
                               : inspectValue(target, watch.address, watch.size, watch.core);
        const bool changed = value.available && data(1, Qt::UserRole).toBool() && text(1) != value.text;
        setText(1, value.text);
        setToolTip(1, value.detail.isEmpty() ? value.text : value.detail);
        setForeground(1, picoThemeColor(changed ? "#ffd780" : value.available ? "#dce6f0" : "#e5ac74"));
        setData(1, Qt::UserRole, value.available);
    }

    ValueWatch watch;
    const picodbg::RegisterDescription* reg;
    bool emulated;
private:
    DebugAccess access_;
};
}

bool ValueWatch::operator==(const ValueWatch& other) const {
    return device == other.device && address == other.address && size == other.size && core == other.core && range == other.range;
}

ValueWatches::ValueWatches(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    empty_ = new QLabel("Ajoutez un suivi depuis Mémoire ou Périphériques.");
    empty_->setWordWrap(true);
    open_ = new QPushButton("↗"); open_->setObjectName("openValueWatchButton");
    memory_ = new QPushButton("▤"); memory_->setObjectName("openWatchMemoryButton");
    remove_ = new QPushButton("−"); remove_->setObjectName("removeValueWatchButton");
    open_->setToolTip("Ouvrir le périphérique ou la zone suivie");
    memory_->setToolTip("Afficher la zone suivie dans Mémoire");
    remove_->setToolTip("Retirer le suivi sélectionné");
    for (auto* button : {open_, memory_, remove_}) {
        button->setFixedWidth(30);
        button->setAccessibleName(button->toolTip());
    }
    auto* actions = new QHBoxLayout;
    actions->addWidget(open_);
    actions->addWidget(memory_);
    actions->addWidget(remove_);
    actions->addStretch();
    tree_ = new QTreeWidget;
    tree_->setObjectName("valueWatchesTree");
    tree_->setAlternatingRowColors(true);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(8);
    tree_->setMinimumWidth(112);
    tree_->setStyleSheet("QHeaderView::section { padding:3px; }");
    tree_->setHeaderLabels({"Nom", "Valeur", "C."});
    tree_->headerItem()->setToolTip(2, "Cœur associé au suivi");
    tree_->headerItem()->setToolTip(0, "Le carré et le fond associent le suivi à sa zone dans Mémoire.\nCouleur du texte\n" + debugAccessLegend());
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    tree_->header()->resizeSection(1, 82);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addLayout(actions);
    layout->addWidget(empty_);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { refreshActions(); });
    connect(open_, &QPushButton::clicked, this, [this] {
        if (auto* item = static_cast<WatchItem*>(tree_->currentItem())) if (open) open(item->watch);
    });
    connect(memory_, &QPushButton::clicked, this, [this] {
        if (auto* item = static_cast<WatchItem*>(tree_->currentItem())) if (openMemory) openMemory(item->watch);
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        // The branch indicator and double-click both expand whole peripherals.
        const auto watch = static_cast<WatchItem*>(item)->watch;
        if (!item->childCount() && open) open(watch);
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto* item = static_cast<WatchItem*>(tree_->currentItem());
        if (item && !item->parent()) remove(item->watch);
    });
    connect(tree_, &QTreeWidget::itemExpanded, this, [this] { if (changed) changed(); });
    refreshActions();
}

void ValueWatches::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    tree_->setColumnHidden(2, event->size().width() < 176);
}

void ValueWatches::updateTarget(const picodbg::DebugTarget& target) {
    if (!initialized_ || chip_ != target.description().chip) {
        initialized_ = true;
        chip_ = target.description().chip;
        const QSignalBlocker blocker(tree_);
        tree_->clear();
        devices_ = picodbg::peripheralCatalog(chip_);
        for (const auto& watch : watches_[chip_]) tree_->addTopLevelItem(makeItem(watch));
        refreshActions();
    }
    for (int i = 0; i < tree_->topLevelItemCount(); ++i)
        static_cast<WatchItem*>(tree_->topLevelItem(i))->refresh(target);
}

bool ValueWatches::contains(const ValueWatch& watch) const {
    const auto it = watches_.find(chip_);
    return it != watches_.end() && std::find(it->second.begin(), it->second.end(), watch) != it->second.end();
}

void ValueWatches::add(const ValueWatch& watch) {
    if (contains(watch)) return;
    auto entry = watch;
    entry.color = static_cast<int>(nextColor_++ % watchColorCount);
    watches_[chip_].push_back(entry);
    auto* item = makeItem(entry);
    {
        const QSignalBlocker blocker(tree_);
        tree_->addTopLevelItem(item);
        item->setExpanded(watch.range);
        tree_->setCurrentItem(item);
    }
    refreshActions();
    if (changed) changed();
}

void ValueWatches::remove(const ValueWatch& watch) {
    auto& watches = watches_[chip_];
    const auto match = std::find(watches.begin(), watches.end(), watch);
    if (match == watches.end()) return;
    const auto index = static_cast<int>(match - watches.begin());
    watches.erase(match);
    delete tree_->takeTopLevelItem(index);
    refreshActions();
    if (changed) changed();
}

int ValueWatches::count() const { return tree_->topLevelItemCount(); }

std::vector<MemoryHighlight> ValueWatches::memoryHighlights(const picodbg::ChipDescription& chip, unsigned core) const {
    std::vector<MemoryHighlight> highlights;
    const auto found = watches_.find(chip.chip);
    if (found == watches_.end()) return highlights;
    for (const auto& watch : found->second) {
        if (watch.core != core) continue;
        uint32_t start = watch.address;
        uint64_t end = uint64_t(start) + watch.size;
        if (!watch.size) {
            const auto device = std::find_if(devices_.begin(), devices_.end(), [&watch](const picodbg::PeripheralDescription& entry) {
                return q(entry.name) == watch.device;
            });
            if (device == devices_.end()) continue;
            start = device->base;
            const auto* region = picodbg::regionAt(chip, start);
            if (!region) continue;
            end = region->end;
        }
        highlights.push_back({start, end, watchColor(watch), watch.device.isEmpty() ? q(picodbg::hex(start)) : watch.device});
    }
    return highlights;
}

QTreeWidgetItem* ValueWatches::makeItem(const ValueWatch& watch) {
    const auto device = std::find_if(devices_.begin(), devices_.end(), [&watch](const picodbg::PeripheralDescription& entry) {
        return q(entry.name) == watch.device;
    });
    const bool memory = watch.device.isEmpty();
    const bool emulated = memory || (device != devices_.end() && device->coverage != "Non émulé");
    const picodbg::RegisterDescription* reg = nullptr;
    if (watch.size && device != devices_.end()) {
        const auto match = std::find_if(device->registers.begin(), device->registers.end(),
                                       [&watch](const picodbg::RegisterDescription& entry) { return entry.address == watch.address; });
        if (match != device->registers.end()) reg = &*match;
    }
    const auto access = device != devices_.end() && !watch.size ? peripheralAccess(*device) : DebugAccess::Unknown;
    auto* item = new WatchItem(watch, reg, emulated, access);
    item->setText(2, QString::number(watch.core));
    item->setTextAlignment(2, Qt::AlignCenter);
    if (memory) {
        item->setText(0, q(picodbg::hex(watch.address)));
        if (watch.range) {
            item->setText(1, QString("%1 octets").arg(watch.size));
            for (unsigned offset = 0; offset < watch.size; offset += rangeRowSize) {
                const auto at = watch.address + offset;
                auto* child = new WatchItem({{}, at, std::min(rangeRowSize, watch.size - offset), watch.core, true, watch.color}, nullptr, true);
                child->setText(0, q(picodbg::hex(at)));
                item->addChild(child);
            }
        }
    } else if (reg) {
        const auto name = q(reg->name);
        item->setText(0, name.startsWith(watch.device + ".") ? name : watch.device + "." + name);
        item->setToolTip(0, item->text(0) + " · " + q(picodbg::hex(reg->address)) + " · " + registerAccessText(*reg));
    } else if (!watch.size && device != devices_.end()) {
        item->setText(0, watch.device);
        item->setText(1, QString("%1 registres").arg(static_cast<qulonglong>(device->registers.size())));
        item->setToolTip(0, q(picodbg::hex(device->base)) + " · " + debugAccessText(access));
        for (const auto& description : device->registers) {
            auto* child = new WatchItem({watch.device, description.address, 4, watch.core, false, watch.color}, &description, emulated);
            child->setText(0, q(description.name));
            child->setToolTip(0, child->text(0) + " · " + q(picodbg::hex(description.address)) + " · " + registerAccessText(description));
            item->addChild(child);
        }
    }
    return item;
}

void ValueWatches::refreshActions() {
    empty_->setVisible(!count());
    open_->setEnabled(tree_->currentItem());
    memory_->setEnabled(tree_->currentItem());
    remove_->setEnabled(tree_->currentItem() && !tree_->currentItem()->parent());
    remove_->setToolTip(tree_->currentItem() && tree_->currentItem()->parent()
        ? "Sélectionnez le groupe pour retirer le suivi de la zone entière" : QString());
}
