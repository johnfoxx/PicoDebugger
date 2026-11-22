#include "peripheral_panel.hpp"
#include "debug_access.hpp"
#include "debug_value.hpp"
#include "register_inspector.hpp"
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

using namespace picodbg;
namespace {
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
        if (openMemory && selected_ >= 0) openMemory(devices_[static_cast<size_t>(selected_)].base, core_);
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
        try { debugger_->readRegisterOnce(reg->address, core_); refreshValues(); }
        catch (const std::exception& e) { detail_->setText(QString::fromUtf8(e.what())); detail_->show(); }
    });
    auto* tools = new QHBoxLayout; tools->addWidget(filter_, 1); tools->addWidget(open_);
    empty_ = new QLabel("Aucun périphérique ne correspond. Effacez le filtre pour retrouver le catalogue.");
    empty_->setWordWrap(true); empty_->hide(); empty_->setObjectName("peripheralEmptyState");
    auto* heading = new QHBoxLayout;
    heading->addWidget(context_, 1);
    heading->addWidget(memory_);
    heading->addWidget(watchDevice_);
    layout->addLayout(tools); layout->addLayout(heading); layout->addWidget(empty_); layout->addWidget(split, 1); layout->addWidget(detail_);
    if (expanded) bits_->hide();
    else connect(bits_, &QPushButton::toggled, this, [this, registerViews](bool checked) {
        registerViews->setCurrentIndex(checked ? 1 : 0); bits_->setText(checked ? "Registres" : "Bits");
    });
    connect(registerFilter_, &QLineEdit::textChanged, this, [this] { filterRegisters(); });
    connect(registers_, &QTableWidget::itemSelectionChanged, this, [this] { refreshRegisterDetails(); });
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
    connect(catalog_, &QTreeWidget::itemSelectionChanged, this, [this] { selectDevice(); });
}
void PeripheralPanel::updateTarget(const DebugTarget& debugger, unsigned core, bool running) {
    const bool changed = debugger_ != &debugger || chip_ != debugger.description().chip || hardware_ != debugger.hardware();
    if (!changed && core_ != core) {
        for (int row = 0; row < registers_->rowCount(); ++row) registers_->item(row, 1)->setData(Qt::UserRole, false);
        inspector_->clear();
    }
    hardware_ = debugger.hardware(); running_ = running;
    readOnce_->setVisible(hardware_);
    debugger_ = &debugger; core_ = core; chip_ = debugger.description().chip;
    if (changed) rebuildCatalog();
    updateHeading();
    refreshValues();
}
void PeripheralPanel::rebuildCatalog() {
    const QSignalBlocker blocker(catalog_);
    inspector_->clear();
    devices_ = peripheralCatalog(chip_); catalog_->clear(); selected_ = -1;
    QTreeWidgetItem* initial = nullptr;
    for (size_t i = 0; i < devices_.size(); ++i) {
        const auto& device = devices_[i];
        const auto coverage = hardware_ ? QString(device.registers.empty() ? "Sans registre" : "Décrit")
                                        : q(device.coverage.substr(0, device.coverage.find(" (")));
        auto* item = new QTreeWidgetItem(catalog_, {q(device.name), coverage});
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setToolTip(0, q(device.name + " · " + hex(device.base)) + "\n" + debugAccessText(peripheralAccess(device)));
        item->setToolTip(1, hardware_ ? QString("Catalogue de registres pour la cible matérielle") : q(device.coverage + " · " + device.note));
        if (q(device.name) == (pinnedDevice_.isEmpty() ? QString("SIO") : pinnedDevice_)) initial = item;
    }
    refreshCatalogColors();
    if (initial) catalog_->setCurrentItem(initial);
    applyFilter(); selectDevice();
}
void PeripheralPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) refreshCatalogColors();
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
        const auto& device = devices_.at(static_cast<size_t>(item->data(0, Qt::UserRole).toInt()));
        const auto access = peripheralAccess(device);
        item->setIcon(0, debugAccessIcon(access));
        item->setForeground(0, debugAccessColor(access));
        item->setForeground(1, !hardware_ && device.coverage == "Non émulé" ? unavailable : supported);
    }
}
QString PeripheralPanel::selectedDevice() const {
    return selected_ < 0 ? QString() : q(devices_.at(static_cast<size_t>(selected_)).name);
}
void PeripheralPanel::applyFilter() {
    const auto needle = filter_->text().trimmed();
    QTreeWidgetItem* first = nullptr;
    for (int i = 0; i < catalog_->topLevelItemCount(); ++i) {
        auto* item = catalog_->topLevelItem(i);
        const auto& device = devices_.at(static_cast<size_t>(item->data(0, Qt::UserRole).toInt()));
        QString searchable = q(device.name + " " + device.coverage + " " + hex(device.base));
        for (const auto& reg : device.registers) {
            searchable += " " + q(reg.name + " " + hex(reg.address));
            for (const auto& field : reg.fields) searchable += " " + q(field.name);
        }
        item->setHidden(!searchable.contains(needle, Qt::CaseInsensitive));
        if (!item->isHidden() && !first) first = item;
    }
    empty_->setVisible(first == nullptr && pinnedDevice_.isEmpty());
    if (!catalog_->currentItem() || catalog_->currentItem()->isHidden()) catalog_->setCurrentItem(first);
}
void PeripheralPanel::selectDevice() {
    auto* item = catalog_->currentItem();
    selected_ = item ? item->data(0, Qt::UserRole).toInt() : -1;
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
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    detail_->setText(hardware_ ? QString("Valeurs matérielles à l’arrêt. Les lectures sensibles sont manuelles.") : q(device.note));
    context_->setToolTip(detail_->text());
    detail_->setToolTip(detail_->text());
    registers_->setRowCount(static_cast<int>(device.registers.size()));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        const QString note = hardware_ ? QString() : q(reg.note);
        const QStringList values = {q(reg.name), "—", q(hex(reg.address)), q(reg.access), note};
        for (int col = 0; col < values.size(); ++col) {
            auto* value = new QTableWidgetItem(values[col]);
            value->setToolTip(col == 3 ? registerAccessText(reg)
                                      : q(reg.name + " · " + hex(reg.address)) + (note.isEmpty() ? QString() : "\n" + note));
            if (col == 3) value->setTextAlignment(Qt::AlignCenter);
            registers_->setItem(static_cast<int>(i), col, value);
        }
    }
    filterRegisters(); refreshValues();
    if (selectionChanged) selectionChanged(selectedDevice());
}
void PeripheralPanel::updateHeading() {
    if (selected_ < 0) {
        context_->setText(pinnedDevice_.isEmpty() ? QString("Catalogue · cœur %1").arg(core_) : pinnedDevice_);
        return;
    }
    const auto& device = devices_[static_cast<size_t>(selected_)];
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
    const auto& regs = devices_[static_cast<size_t>(selected_)].registers;
    for (size_t i = 0; i < regs.size(); ++i) {
        if (regs[i].address != address) continue;
        registers_->setCurrentCell(static_cast<int>(i), 0);
        registers_->scrollToItem(registers_->item(static_cast<int>(i), 0));
        return;
    }
}
void PeripheralPanel::refreshValues() {
    if (!debugger_ || selected_ < 0) return;
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        auto* value = registers_->item(static_cast<int>(i), 1);
        auto* accessItem = registers_->item(static_cast<int>(i), 3);
        auto* fields = registers_->item(static_cast<int>(i), 4);
        const auto access = registerAccess(reg);
        accessItem->setIcon(debugAccessIcon(access));
        accessItem->setForeground(debugAccessColor(access));
        const auto sample = inspectRegister(*debugger_, reg, core_, device.coverage != "Non émulé");
        const auto decoded = sample.available ? fieldText(reg, sample.raw) : QString();
        const auto description = decoded.isEmpty() && !hardware_ ? q(reg.note) : decoded;
        fields->setText(description); fields->setToolTip(description);
        const bool changed = sample.available && value->data(Qt::UserRole).toBool() && value->text() != sample.text;
        value->setForeground(picoThemeColor(changed ? "#ffd780" : sample.available ? "#dce6f0" : "#e5ac74"));
        value->setText(sample.text); value->setToolTip(sample.detail.isEmpty() ? description : sample.detail);
        value->setData(Qt::UserRole, sample.available);
        value->setData(Qt::UserRole + 1, sample.available ? QVariant(sample.raw) : QVariant());
    }
    refreshRegisterDetails();
}
void PeripheralPanel::filterRegisters() {
    if (selected_ < 0) return;
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    const auto needle = registerFilter_->text().trimmed();
    int first = -1;
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        QString text = q(reg.name + " " + hex(reg.address));
        for (const auto& field : reg.fields) text += " " + q(field.name);
        const bool visible = text.contains(needle, Qt::CaseInsensitive);
        registers_->setRowHidden(static_cast<int>(i), !visible);
        if (visible && first < 0) first = static_cast<int>(i);
    }
    registerEmpty_->setText(device.registers.empty() ? "Aucun registre dans le catalogue." : "Aucun registre ne correspond au filtre.");
    registerEmpty_->setVisible(first < 0);
    if (registers_->currentRow() < 0 || registers_->isRowHidden(registers_->currentRow()) || registers_->selectedItems().isEmpty()) {
        if (first >= 0) registers_->setCurrentCell(first, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        else { registers_->clearSelection(); inspector_->clear(); }
    }
    refreshRegisterDetails();
}
const RegisterDescription* PeripheralPanel::selectedRegister() const {
    const int row = registers_->currentRow();
    if (selected_ < 0 || row < 0 || registers_->isRowHidden(row) || registers_->selectedItems().isEmpty()) return nullptr;
    const auto& regs = devices_[static_cast<size_t>(selected_)].registers;
    return static_cast<size_t>(row) < regs.size() ? &regs[static_cast<size_t>(row)] : nullptr;
}
void PeripheralPanel::refreshRegisterDetails() {
    const auto* reg = selectedRegister();
    const auto* device = selected_ < 0 ? nullptr : &devices_[static_cast<size_t>(selected_)];
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
    writeRegister_->setEnabled(editRegister && ready && reg && registerWritable(*reg) && (hardware_ || device->coverage != "Non émulé"));
    writeRegister_->setToolTip(!ready ? "Mettre la cible en pause pour écrire" :
        !reg ? "Sélectionner un registre" : registerAccessText(*reg));
    readOnce_->setEnabled(ready && reg && reg->access != "WO");
    if (!reg) { inspector_->clear(); return; }
    auto* value = registers_->item(registers_->currentRow(), 1);
    if (!value) return;
    auto description = *reg;
    if (hardware_) description.note = registerAccessText(*reg).toStdString();
    inspector_->setRegister(description);
    inspector_->setValue(value->data(Qt::UserRole).toBool(), value->data(Qt::UserRole + 1).toUInt(), value->text());
}
