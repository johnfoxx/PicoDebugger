#include "peripheral_panel.hpp"
#include "debug_access.hpp"
#include "debug_value.hpp"
#include "register_inspector.hpp"
#include "svd_catalog.hpp"
#include "window_theme.hpp"
#include <QAction>
#include <QEvent>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

using namespace picodbg;
namespace {
constexpr int availableRole = Qt::UserRole;
constexpr int rawRole = Qt::UserRole + 1;
constexpr int errorRole = Qt::UserRole + 2;
QString q(const std::string& text) { return QString::fromStdString(text); }
QString fieldText(const RegisterDescription& reg, uint32_t value) {
    QStringList fields;
    for (const auto& field : reg.fields)
        fields << q(field.name) + "=" + QString::number(fieldValue(value, field));
    return fields.join("  ");
}
}
PeripheralPanel::PeripheralPanel(QWidget* parent, const QString& pinnedDevice, bool expanded) : QWidget(parent), pinnedDevice_(pinnedDevice) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);
    filter_ = new QLineEdit; filter_->setObjectName("peripheralFilter");
    filter_->setClearButtonEnabled(true); filter_->setAccessibleName("Rechercher un périphérique");
    filter_->setPlaceholderText("Rechercher un périphérique, registre ou adresse…");
    context_ = new QLabel; context_->setWordWrap(true); context_->setObjectName("sectionTitle");
    catalog_ = new QTreeWidget; catalog_->setObjectName("peripheralCatalog");
    catalog_->setHeaderLabels({"Bloc", "Modèle"});
    catalog_->setMinimumWidth(193);
    catalog_->headerItem()->setToolTip(0, debugAccessLegend());
    catalog_->setRootIsDecorated(false); catalog_->setAlternatingRowColors(true);
    catalog_->setUniformRowHeights(true);
    catalog_->header()->setStretchLastSection(false);
    catalog_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    catalog_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    registers_ = new QTableWidget(0, 5); registers_->setObjectName("peripheralsTable");
    registers_->setHorizontalHeaderLabels({"Registre", "Valeur", "Adresse", "Accès", "Champs / note"});
    registers_->horizontalHeaderItem(3)->setToolTip(debugAccessLegend());
    registers_->verticalHeader()->hide(); registers_->verticalHeader()->setDefaultSectionSize(23);
    registers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    registers_->setSelectionBehavior(QAbstractItemView::SelectRows);
    registers_->setAlternatingRowColors(true); registers_->setShowGrid(false);
    registers_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    registers_->installEventFilter(this);
    registers_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    registers_->horizontalHeader()->setResizeContentsPrecision(64);
    registers_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    registerFilter_ = new QLineEdit; registerFilter_->setObjectName("registerFilter");
    registerFilter_->setClearButtonEnabled(true); registerFilter_->setAccessibleName("Filtrer les registres");
    registerFilter_->setPlaceholderText("Nom, adresse ou champ…");
    auto* registerPane = new QWidget; auto* registerLayout = new QVBoxLayout(registerPane);
    registerLayout->setContentsMargins(0, 0, 0, 0);
    registerLayout->setSpacing(4);
    auto* registerTools = new QHBoxLayout;
    registerTools->addWidget(registerFilter_, 1);
    registerLayout->addLayout(registerTools);
    registerEmpty_ = new QLabel;
    registerEmpty_->setWordWrap(true);
    registerLayout->addWidget(registerEmpty_);
    inspector_ = new RegisterInspector;
    inspector_->editField = [this](const RegisterField& field) {
        const auto* reg = selectedRegister();
        if (!reg || !editField || !writeRegister_->isEnabled()) return;
        editField(*reg, field, core_, registers_->item(registers_->currentRow(), 1)->data(rawRole).toUInt());
    };
    QStackedWidget* registerViews = nullptr;
    if (expanded) {
        auto* details = new QSplitter(Qt::Vertical);
        details->setObjectName("registerDetailsSplitter"); details->setChildrenCollapsible(false);
        details->addWidget(registers_); details->addWidget(inspector_);
        details->setSizes({300, 220}); registerLayout->addWidget(details, 1);
    } else {
        registerViews = new QStackedWidget;
        registerViews->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        registerViews->setMinimumHeight(62);
        registerViews->addWidget(registers_); registerViews->addWidget(inspector_);
        registerLayout->addWidget(registerViews, 1);
    }
    detail_ = new QLabel; detail_->setWordWrap(true); detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Long device prose is available from the context tooltip. Keeping it
    // outside the layout leaves room for values/fields on a laptop screen.
    detail_->hide();
    if (expanded) { detail_->show(); detail_->setMaximumHeight(48); }
    auto* split = new QSplitter(Qt::Horizontal); split->setChildrenCollapsible(false);
    split->addWidget(catalog_); split->addWidget(registerPane); split->setSizes({210, 420});
    open_ = new QPushButton("Zoom"); open_->setObjectName("openDeviceWindowButton");
    open_->setToolTip("Ouvrir une vue dédiée au périphérique sélectionné");
    watchDevice_ = new QPushButton("Suivre le bloc"); watchDevice_->setObjectName("watchPeripheralButton");
    watchDevice_->setCheckable(true); watchDevice_->setToolTip("Suivre tous les registres du périphérique");
    memory_ = new QPushButton("Mémoire"); memory_->setObjectName("openPeripheralMemoryButton");
    memory_->setToolTip("Voir la plage d’adresses du périphérique en mémoire");
    connect(memory_, &QPushButton::clicked, this, [this] {
        if (openMemory && selected_ >= 0) openMemory(devices_->at(static_cast<size_t>(selected_)).base, core_);
    });
    auto* actions = new QToolButton;
    actions->setText("Actions");
    actions->setPopupMode(QToolButton::InstantPopup);
    actions->setToolTip("Écrire, suivre ou lire le registre sélectionné. Les mêmes commandes sont disponibles par clic droit.");
    auto* registerMenu = new QMenu(actions);
    registerMenu->setToolTipsVisible(true);
    actions->setMenu(registerMenu);
    registerTools->addWidget(actions);
    writeRegister_ = registerMenu->addAction("Écrire…"); writeRegister_->setObjectName("writePeripheralRegisterAction");
    watchRegister_ = registerMenu->addAction("Suivre le registre"); watchRegister_->setObjectName("watchRegisterAction");
    watchRegister_->setCheckable(true);
    watchRegister_->setToolTip("Ajouter ou retirer le registre sélectionné du suivi");
    connect(watchDevice_, &QPushButton::clicked, this, [this] {
        if (toggleWatch && selected_ >= 0) toggleWatch(selectedDevice(), 0, 0, core_);
    });
    connect(watchRegister_, &QAction::triggered, this, [this] {
        if (const auto* reg = selectedRegister()) if (toggleWatch) toggleWatch(selectedDevice(), reg->address, 4, core_);
    });
    connect(writeRegister_, &QAction::triggered, this, [this] {
        if (const auto* reg = selectedRegister()) if (editRegister) {
            const auto* value = registers_->item(registers_->currentRow(), 1);
            editRegister(selectedDevice(), *reg, core_, value->text());
        }
    });
    bits_ = new QPushButton("Bits"); bits_->setObjectName("registerDetailsButton"); bits_->setCheckable(true);
    bits_->setToolTip("Afficher les champs et la signification des bits du registre sélectionné");
    registerTools->addWidget(bits_);
    readOnce_ = registerMenu->addAction("Lire une fois"); readOnce_->setObjectName("readHardwareRegisterOnce");
    readOnce_->setToolTip("Lire le registre sélectionné sur la carte, y compris une FIFO ou un registre effacé à la lecture. Peut modifier l'état matériel.");
    readOnce_->setVisible(false);
    connect(readOnce_, &QAction::triggered, this, [this] {
        const auto* reg = selectedRegister();
        if (!debugger_ || !reg || reg->access == "WO") return;
        try {
            debugger_->readRegisterOnce(reg->address, core_);
            refreshValues();
            refreshRegisterDetails();
        }
        catch (const std::exception& e) { detail_->setText(QString::fromUtf8(e.what())); detail_->show(); }
    });
    auto* tools = new QHBoxLayout; tools->addWidget(filter_, 1); tools->addWidget(open_);
    empty_ = new QLabel("Aucun périphérique ne correspond. Effacez le filtre pour retrouver le catalogue.");
    empty_->setWordWrap(true); empty_->hide(); empty_->setObjectName("peripheralEmptyState");
    auto* headingPane = new QWidget;
    auto* heading = new QHBoxLayout(headingPane);
    heading->setContentsMargins(0, 0, 0, 0);
    heading->addWidget(context_, 1);
    heading->addWidget(memory_);
    heading->addWidget(watchDevice_);
    layout->addLayout(tools); layout->addWidget(headingPane); layout->addWidget(empty_); layout->addWidget(split, 1); layout->addWidget(detail_);
    if (expanded) bits_->hide();
    else connect(bits_, &QPushButton::toggled, this, [this, registerViews, headingPane](bool checked) {
        if (pinnedDevice_.isEmpty()) {
            filter_->setVisible(!checked);
            open_->setVisible(!checked);
            headingPane->setVisible(!checked);
            catalog_->setVisible(!checked);
        }
        registerViews->setMinimumHeight(checked ? 120 : 62);
        registerViews->setCurrentWidget(checked ? static_cast<QWidget*>(inspector_) : registers_);
        bits_->setText(checked ? "Registres" : "Bits");
        bits_->setToolTip(checked ? "Revenir à la liste des registres"
                                 : "Afficher les champs et la signification des bits du registre sélectionné");
        if (checked) refreshRegisterDetails();
    });
    connect(registerFilter_, &QLineEdit::textChanged, this, [this] { filterRegisters(); });
    connect(registers_, &QTableWidget::itemSelectionChanged, this, [this] { refreshRegisterDetails(); });
    connect(registers_, &QTableWidget::currentCellChanged, this, [this](int row, int, int previousRow, int) {
        if (row != previousRow) refreshRegisterDetails();
    });
    connect(registers_, &QTableWidget::cellDoubleClicked, this, [this](int, int column) {
        if (column == 1 && writeRegister_->isEnabled()) writeRegister_->trigger();
        else bits_->setChecked(true);
    });
    registers_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(registers_, &QTableWidget::customContextMenuRequested, this, [this, registerMenu](const QPoint& point) {
        const auto index = registers_->indexAt(point);
        if (!index.isValid()) return;
        registers_->setCurrentCell(index.row(), index.column());
        registerMenu->popup(registers_->viewport()->mapToGlobal(point));
    });
    connect(open_, &QPushButton::clicked, this, [this] { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    connect(catalog_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    if (!pinnedDevice_.isEmpty()) { filter_->hide(); catalog_->hide(); open_->hide(); }
    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(catalog_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        selectDevice(item ? item->data(0, Qt::UserRole).toInt() : -1);
    });
}
void PeripheralPanel::updateTarget(const DebugTarget& debugger, unsigned core, bool running) {
    const auto devices = picosvd::catalogSnapshot(debugger.description().chip);
    const bool changed = debugger_ != &debugger || devices_ != devices || hardware_ != debugger.hardware();
    const bool coreChanged = core_ != core;
    const bool reread = coreChanged || running_ != running || revision_ != debugger.revision();
    const auto themeColor = picoThemeColor("#dce6f0");
    const bool themeChanged = valueColor_ != themeColor;
    valueColor_ = themeColor;
    if (!changed && coreChanged) {
        for (int row = 0; row < registers_->rowCount(); ++row) registers_->item(row, 1)->setData(availableRole, false);
        inspector_->clear();
    }
    hardware_ = debugger.hardware(); running_ = running;
    readOnce_->setVisible(hardware_);
    debugger_ = &debugger; core_ = core; revision_ = debugger.revision();
    if (changed) {
        devices_ = devices;
        rebuildCatalog();
    } else {
        if (coreChanged) updateHeading();
        if (themeChanged) {
            refreshCatalogColors();
            refreshRegisterColors();
        }
        if (reread) refreshValues();
        refreshRegisterDetails();
    }
    if (!pendingSelection_.isEmpty()) applySavedSelection();
}
void PeripheralPanel::rebuildCatalog() {
    const QSignalBlocker blocker(catalog_);
    catalog_->clear(); selected_ = -1;
    const auto name = pinnedDevice_.isEmpty()
        ? pendingSelection_.value("device", "SIO").toString() : pinnedDevice_;
    if (!pinnedDevice_.isEmpty()) {
        const auto found = std::find_if(devices_->begin(), devices_->end(), [&](const PeripheralDescription& device) {
            return q(device.name) == name;
        });
        selectDevice(found == devices_->end() ? -1 : static_cast<int>(found - devices_->begin()));
        return;
    }
    deviceSearch_.assign(devices_->size(), QString());
    QTreeWidgetItem* initial = nullptr;
    for (size_t i = 0; i < devices_->size(); ++i) {
        const auto& device = devices_->at(i);
        const auto coverage = hardware_ ? QString(device.registers.empty() ? "Sans registre" : "Décrit")
                                        : q(device.coverage.substr(0, device.coverage.find(" (")));
        auto* item = new QTreeWidgetItem(catalog_, {q(device.name), coverage});
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setToolTip(0, q(device.name + " · " + hex(device.base)) + "\n" + debugAccessText(peripheralAccess(device)));
        item->setToolTip(1, hardware_ ? QString("Catalogue de registres pour la cible matérielle") : q(device.coverage + " · " + device.note));
        if (item->text(0) == name) initial = item;
    }
    refreshCatalogColors();
    if (initial) catalog_->setCurrentItem(initial);
    applyFilter();
    const auto* current = catalog_->currentItem();
    selectDevice(current ? current->data(0, Qt::UserRole).toInt() : -1);
}
bool PeripheralPanel::eventFilter(QObject* object, QEvent* event) {
    if (object == registers_ && event->type() == QEvent::Resize) {
        const int width = registers_->width();
        registers_->setColumnHidden(2, width < 460);
        registers_->setColumnHidden(4, width < 630);
    }
    return QWidget::eventFilter(object, event);
}
void PeripheralPanel::refreshCatalogColors() {
    const auto supported = picoThemeColor("#dce6f0");
    const auto unavailable = picoThemeColor("#e5ac74");
    for (int i = 0; i < catalog_->topLevelItemCount(); ++i) {
        auto* item = catalog_->topLevelItem(i);
        const auto& device = devices_->at(static_cast<size_t>(item->data(0, Qt::UserRole).toInt()));
        const auto access = peripheralAccess(device);
        item->setIcon(0, debugAccessIcon(access));
        item->setForeground(0, debugAccessColor(access));
        item->setForeground(1, !hardware_ && device.coverage == "Non émulé" ? unavailable : supported);
    }
}
void PeripheralPanel::refreshRegisterColors() {
    if (selected_ < 0) return;
    const QSignalBlocker blocker(registers_);
    QSignalBlocker changes(registers_->model());
    const auto& regs = devices_->at(static_cast<size_t>(selected_)).registers;
    for (int row = 0; row < registers_->rowCount(); ++row) {
        const auto access = registerAccess(regs[static_cast<size_t>(row)]);
        auto* item = registers_->item(row, 3);
        item->setIcon(debugAccessIcon(access));
        item->setForeground(debugAccessColor(access));
        auto* value = registers_->item(row, 1);
        value->setForeground(picoThemeColor(value->data(availableRole).toBool() ? "#dce6f0" : "#e5ac74"));
    }
    changes.unblock();
    notifyRegisterCells(1, 3);
}
QString PeripheralPanel::selectedDevice() const {
    return selected_ < 0 ? QString() : q(devices_->at(static_cast<size_t>(selected_)).name);
}
QVariantMap PeripheralPanel::saveSelection() const {
    if (!pendingSelection_.isEmpty()) return pendingSelection_;
    QVariantMap selection{
        {"device", selectedDevice()}, {"filter", filter_->text()},
        {"registerFilter", registerFilter_->text()}, {"bits", bits_->isChecked()}
    };
    if (const auto* reg = selectedRegister()) selection.insert("register", reg->address);
    return selection;
}
void PeripheralPanel::restoreSelection(const QVariantMap& selection) {
    pendingSelection_ = selection;
}
void PeripheralPanel::applySavedSelection() {
    const auto selection = std::move(pendingSelection_);
    pendingSelection_.clear();
    if (pinnedDevice_.isEmpty()) {
        const QSignalBlocker catalogueSignals(catalog_), filterSignals(filter_);
        filter_->setText(selection.value("filter").toString());
        const auto matches = catalog_->findItems(selection.value("device", "SIO").toString(), Qt::MatchExactly, 0);
        catalog_->setCurrentItem(matches.isEmpty() ? nullptr : matches.front());
        applyFilter();
        const auto* current = catalog_->currentItem();
        const int index = current ? current->data(0, Qt::UserRole).toInt() : -1;
        if (index != selected_) selectDevice(index);
    }
    registerFilter_->setText(selection.value("registerFilter").toString());
    if (selection.contains("register")) selectRegisterRow(selection.value("register").toUInt());
    bits_->setChecked(selection.value("bits", false).toBool());
}
void PeripheralPanel::applyFilter() {
    const auto needle = filter_->text().trimmed();
    QTreeWidgetItem* first = nullptr;
    for (int i = 0; i < catalog_->topLevelItemCount(); ++i) {
        auto* item = catalog_->topLevelItem(i);
        const auto index = static_cast<size_t>(item->data(0, Qt::UserRole).toInt());
        auto& searchable = deviceSearch_[index];
        if (!needle.isEmpty() && searchable.isEmpty()) {
            const auto& device = devices_->at(index);
            searchable = q(device.name + " " + device.coverage + " " + hex(device.base));
            for (const auto& reg : device.registers) {
                searchable += " " + q(reg.name + " " + hex(reg.address));
                for (const auto& field : reg.fields) searchable += " " + q(field.name);
            }
        }
        item->setHidden(!needle.isEmpty() && !searchable.contains(needle, Qt::CaseInsensitive));
        if (!item->isHidden() && !first) first = item;
    }
    empty_->setVisible(first == nullptr && pinnedDevice_.isEmpty());
    if (!catalog_->currentItem() || catalog_->currentItem()->isHidden()) catalog_->setCurrentItem(first);
}
void PeripheralPanel::selectDevice(int index) {
    const QSignalBlocker blocker(registers_);
    selected_ = index;
    open_->setEnabled(selected_ >= 0);
    inspector_->clear();
    registers_->setRowCount(0);
    updateHeading();
    if (selected_ < 0) {
        detail_->clear(); context_->setToolTip({});
        registerEmpty_->hide();
        refreshRegisterDetails();
        if (selectionChanged) selectionChanged({});
        return;
    }
    const auto& device = devices_->at(static_cast<size_t>(selected_));
    detail_->setText(hardware_ ? QString("Valeurs matérielles à l’arrêt. Les lectures sensibles sont manuelles.") : q(device.note));
    context_->setToolTip(detail_->text());
    detail_->setToolTip(detail_->text());
    registers_->setRowCount(static_cast<int>(device.registers.size()));
    registerSearch_.assign(device.registers.size(), QString());
    QSignalBlocker changes(registers_->model());
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        const QString note = hardware_ ? QString() : q(reg.note);
        const auto name = q(reg.name), address = q(hex(reg.address));
        const auto tip = name + " · " + address + (note.isEmpty() ? QString() : "\n" + note);
        const QStringList values = {name, "—", address, q(reg.access), note};
        for (int col = 0; col < values.size(); ++col) {
            auto* value = new QTableWidgetItem(values[col]);
            value->setToolTip(col == 3 ? registerAccessText(reg) : tip);
            if (col == 3) value->setTextAlignment(Qt::AlignCenter);
            registers_->setItem(static_cast<int>(i), col, value);
        }
    }
    refreshRegisterColors();
    refreshValues();
    changes.unblock();
    notifyRegisterCells(0, registers_->columnCount() - 1);
    filterRegisters();
    if (selectionChanged) selectionChanged(selectedDevice());
}
void PeripheralPanel::updateHeading() {
    if (selected_ < 0) {
        context_->setText(pinnedDevice_.isEmpty() ? QString("Catalogue · cœur %1").arg(core_) : pinnedDevice_);
        return;
    }
    const auto& device = devices_->at(static_cast<size_t>(selected_));
    context_->setText(QString("%1 · %2 · cœur %3").arg(q(device.name), q(hex(device.base))).arg(core_));
}
void PeripheralPanel::focusDevice(const QString& name) {
    const bool general = name.isEmpty();
    filter_->setVisible(general); catalog_->setVisible(general); open_->setVisible(general);
    if (general) { applyFilter(); return; }
    filter_->clear(); registerFilter_->clear();
    const auto matches = catalog_->findItems(name, Qt::MatchExactly, 0);
    if (!matches.isEmpty()) catalog_->setCurrentItem(matches.front());
}
void PeripheralPanel::selectRegister(uint32_t address) {
    if (selected_ < 0) return;
    bits_->setChecked(false);
    registerFilter_->clear();
    selectRegisterRow(address);
}
void PeripheralPanel::selectRegisterRow(uint32_t address) {
    if (selected_ < 0) return;
    const auto& regs = devices_->at(static_cast<size_t>(selected_)).registers;
    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i].address != address || registers_->isRowHidden(static_cast<int>(i))) continue;
        registers_->setCurrentCell(static_cast<int>(i), 0);
        registers_->scrollToItem(registers_->item(static_cast<int>(i), 0));
        return;
    }
}
void PeripheralPanel::refreshValues() {
    if (!debugger_ || selected_ < 0) return;
    const QSignalBlocker blocker(registers_);
    QSignalBlocker changes(registers_->model());
    bool cellsChanged = false;
    const auto normal = picoThemeColor("#dce6f0"), unavailable = picoThemeColor("#e5ac74"), changedColor = picoThemeColor("#ffd780");
    const auto& device = devices_->at(static_cast<size_t>(selected_));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        auto* value = registers_->item(static_cast<int>(i), 1);
        if (reg.documentedOnly && !hardware_ && value->text() == "Non émulé") continue;
        const auto sample = inspectRegister(*debugger_, reg, core_, device.coverage != "Non émulé");
        const bool hadValue = value->data(availableRole).toBool();
        const bool changed = sample.available && hadValue && value->data(rawRole).toUInt() != sample.raw;
        const auto color = changed ? changedColor : sample.available ? normal : unavailable;
        if (value->foreground().color() != color) {
            value->setForeground(color);
            cellsChanged = true;
        }
        if (sample.available == hadValue && (sample.available ? !changed
            : value->text() == sample.text && value->data(errorRole).toString() == sample.detail)) continue;
        const auto decoded = sample.available ? fieldText(reg, sample.raw) : QString();
        const auto description = decoded.isEmpty() && !hardware_ ? q(reg.note) : decoded;
        auto* fields = registers_->item(static_cast<int>(i), 4);
        fields->setText(description); fields->setToolTip(description);
        value->setText(sample.text); value->setToolTip(sample.detail.isEmpty() ? description : sample.detail);
        value->setData(availableRole, sample.available);
        value->setData(rawRole, sample.available ? QVariant(sample.raw) : QVariant());
        value->setData(errorRole, sample.detail);
        cellsChanged = true;
    }
    changes.unblock();
    if (cellsChanged) notifyRegisterCells(1, 4);
}
void PeripheralPanel::notifyRegisterCells(int firstColumn, int lastColumn) {
    if (registers_->rowCount() == 0) return;
    auto* model = registers_->model();
    model->dataChanged(model->index(0, firstColumn), model->index(registers_->rowCount() - 1, lastColumn));
}
void PeripheralPanel::filterRegisters() {
    if (selected_ < 0) return;
    const QSignalBlocker blocker(registers_);
    const auto& device = devices_->at(static_cast<size_t>(selected_));
    const auto needle = registerFilter_->text().trimmed();
    int first = -1;
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        auto& text = registerSearch_[i];
        if (!needle.isEmpty() && text.isEmpty()) {
            text = q(reg.name + " " + hex(reg.address));
            for (const auto& field : reg.fields) text += " " + q(field.name);
        }
        const int row = static_cast<int>(i);
        const bool visible = needle.isEmpty() || text.contains(needle, Qt::CaseInsensitive);
        if (registers_->isRowHidden(row) == visible) registers_->setRowHidden(row, !visible);
        if (visible && first < 0) first = static_cast<int>(i);
    }
    registerEmpty_->setText(device.registers.empty() ? "Aucun registre dans le catalogue." : "Aucun registre ne correspond au filtre.");
    registerEmpty_->setVisible(first < 0);
    if (registers_->currentRow() < 0 || registers_->isRowHidden(registers_->currentRow()) || !registers_->selectionModel()->hasSelection()) {
        if (first >= 0) registers_->setCurrentCell(first, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        else { registers_->clearSelection(); inspector_->clear(); }
    }
    refreshRegisterDetails();
}
const RegisterDescription* PeripheralPanel::selectedRegister() const {
    const int row = registers_->currentRow();
    if (selected_ < 0 || row < 0 || registers_->isRowHidden(row) || !registers_->selectionModel()->hasSelection()) return nullptr;
    const auto& regs = devices_->at(static_cast<size_t>(selected_)).registers;
    return static_cast<size_t>(row) < regs.size() ? &regs[static_cast<size_t>(row)] : nullptr;
}
void PeripheralPanel::refreshRegisterDetails() {
    const auto* reg = selectedRegister();
    const auto* device = selected_ < 0 ? nullptr : &devices_->at(static_cast<size_t>(selected_));
    const bool ready = debugger_ && debugger_->loaded() && !running_ && !debugger_->executing() && !debugger_->busy();
    const auto name = selectedDevice();
    memory_->setEnabled(openMemory && device);
    watchDevice_->setEnabled(toggleWatch && device && !device->registers.empty());
    watchDevice_->setChecked(watchDevice_->isEnabled() && isWatched && isWatched(name, 0, 0, core_));
    watchDevice_->setText(watchDevice_->isChecked() ? "Bloc suivi" : "Suivre le bloc");
    watchDevice_->setToolTip(watchDevice_->isChecked() ? "Retirer le périphérique du suivi" : "Suivre tous les registres du périphérique");
    watchRegister_->setEnabled(toggleWatch && reg);
    watchRegister_->setChecked(watchRegister_->isEnabled() && isWatched && isWatched(name, reg->address, 4, core_));
    watchRegister_->setText(watchRegister_->isChecked() ? "Retirer le registre du suivi" : "Suivre le registre");
    writeRegister_->setEnabled(editRegister && ready && reg && !reg->documentedOnly && registerWritable(*reg) &&
                               (hardware_ || device->coverage != "Non émulé"));
    writeRegister_->setToolTip(!ready ? "Mettre la cible en pause pour écrire" :
        !reg ? "Sélectionner un registre" : reg->documentedOnly ? "Registre disponible pour consultation" : registerAccessText(*reg));
    readOnce_->setEnabled(ready && reg && reg->access != "WO");
    if (!reg) { inspector_->clear(); return; }
    auto* value = registers_->item(registers_->currentRow(), 1);
    if (!value) return;
    auto description = *reg;
    if (hardware_) description.note = registerAccessText(*reg).toStdString();
    inspector_->setRegister(description);
    inspector_->setValue(value->data(availableRole).toBool(), value->data(rawRole).toUInt(), value->text(),
                         writeRegister_->isEnabled() && static_cast<bool>(editField));
}
