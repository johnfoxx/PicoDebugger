#include "peripheral_panel.hpp"
#include "register_inspector.hpp"
#include "window_theme.hpp"
#include <QEvent>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
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
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit; filter_->setObjectName("peripheralFilter");
    filter_->setClearButtonEnabled(true); filter_->setAccessibleName("Rechercher un périphérique");
    filter_->setPlaceholderText("Rechercher un périphérique, registre ou adresse…");
    context_ = new QLabel; context_->setWordWrap(true);
    catalog_ = new QTreeWidget; catalog_->setObjectName("peripheralCatalog");
    catalog_->setHeaderLabels({"Périphérique", "Couverture"});
    catalog_->setRootIsDecorated(false); catalog_->setAlternatingRowColors(true);
    catalog_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    catalog_->header()->setStretchLastSection(true);
    registers_ = new QTableWidget(0, 5); registers_->setObjectName("peripheralsTable");
    registers_->setHorizontalHeaderLabels({"Registre", "Valeur", "Adresse", "Accès", "Champs / description"});
    registers_->verticalHeader()->hide(); registers_->verticalHeader()->setDefaultSectionSize(23);
    registers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    registers_->setSelectionBehavior(QAbstractItemView::SelectRows);
    registers_->setAlternatingRowColors(true); registers_->setShowGrid(false);
    registers_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    registers_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    registers_->horizontalHeader()->setStretchLastSection(false);
    registerFilter_ = new QLineEdit; registerFilter_->setObjectName("registerFilter");
    registerFilter_->setClearButtonEnabled(true); registerFilter_->setAccessibleName("Filtrer les registres");
    registerFilter_->setPlaceholderText("Filtrer les registres : nom, adresse ou champ…");
    auto* registerPane = new QWidget; auto* registerLayout = new QVBoxLayout(registerPane);
    registerLayout->setContentsMargins(0, 0, 0, 0);
    auto* registerTools = new QHBoxLayout;
    registerTools->addWidget(registerFilter_, 1);
    registerLayout->addLayout(registerTools);
    inspector_ = new RegisterInspector;
    QStackedWidget* registerViews = nullptr;
    if (expanded) {
        auto* details = new QSplitter(Qt::Vertical);
        details->setObjectName("registerDetailsSplitter"); details->setChildrenCollapsible(false);
        details->addWidget(registers_); details->addWidget(inspector_);
        details->setSizes({300, 220}); registerLayout->addWidget(details, 1);
        registers_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    } else {
        registerViews = new QStackedWidget;
        registerViews->addWidget(registers_); registerViews->addWidget(inspector_);
        registerLayout->addWidget(registerViews, 1);
    }
    detail_ = new QLabel; detail_->setWordWrap(true); detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Long device prose is available from the context tooltip. Keeping it
    // outside the layout leaves room for values/fields on a laptop screen.
    detail_->hide();
    if (expanded) { detail_->show(); detail_->setMaximumHeight(48); }
    auto* split = new QSplitter(Qt::Horizontal); split->setChildrenCollapsible(false);
    split->addWidget(catalog_); split->addWidget(registerPane); split->setSizes({210, 620});
    open_ = new QPushButton("Inspecter"); open_->setObjectName("openDeviceWindowButton");
    open_->setToolTip("Ouvrir ou retrouver l'onglet de ce périphérique ; détachement disponible dans l'inspecteur");
    auto* bits = new QPushButton("Bits"); bits->setObjectName("registerDetailsButton"); bits->setCheckable(true);
    bits->setToolTip("Afficher les champs et la signification des bits du registre sélectionné");
    registerTools->addWidget(bits);
    readOnce_ = new QPushButton("Lire une fois · effets possibles"); readOnce_->setObjectName("readHardwareRegisterOnce");
    readOnce_->setToolTip("Lire le registre sélectionné sur la carte, y compris une FIFO ou un registre effacé à la lecture. Peut modifier l'état matériel.");
    registerTools->addWidget(readOnce_); readOnce_->hide();
    connect(readOnce_, &QPushButton::clicked, this, [this] {
        const int row = registers_->currentRow();
        if (!debugger_ || selected_ < 0 || row < 0) return;
        const auto& reg = devices_[static_cast<size_t>(selected_)].registers[static_cast<size_t>(row)];
        if (reg.access == "WO") return;
        try { debugger_->readRegisterOnce(reg.address, core_); refreshValues(); }
        catch (const std::exception& e) { detail_->setText(QString::fromUtf8(e.what())); detail_->show(); }
    });
    auto* tools = new QHBoxLayout; tools->addWidget(filter_, 1); tools->addWidget(open_);
    if (pinnedDevice_.isEmpty() && !expanded) {
        auto* separate = new QPushButton("↗ Fenêtre"); separate->setObjectName("openPeripheralExplorerButton");
        separate->setToolTip("Ouvrir la fenêtre des périphériques et garder le code visible (Ctrl+Maj+I)");
        tools->addWidget(separate);
        connect(separate, &QPushButton::clicked, this, [this] { if (openExplorer) openExplorer(); });
    }
    empty_ = new QLabel("Aucun périphérique ne correspond. Effacez le filtre pour retrouver le catalogue.");
    empty_->setWordWrap(true); empty_->hide(); empty_->setObjectName("peripheralEmptyState");
    layout->addLayout(tools); layout->addWidget(context_); layout->addWidget(empty_); layout->addWidget(split, 1); layout->addWidget(detail_);
    if (expanded) { bits->hide(); open_->setText("Isoler ce périphérique"); }
    else connect(bits, &QPushButton::toggled, this, [registerViews, bits](bool checked) {
        registerViews->setCurrentIndex(checked ? 1 : 0); bits->setText(checked ? "Registres" : "Bits");
    });
    connect(registerFilter_, &QLineEdit::textChanged, this, [this] { filterRegisters(); });
    connect(registers_, &QTableWidget::itemSelectionChanged, this, [this] { refreshRegisterDetails(); });
    connect(registers_, &QTableWidget::cellDoubleClicked, this, [bits](int, int) { bits->setChecked(true); });
    connect(open_, &QPushButton::clicked, this, [this] { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    connect(catalog_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    if (!pinnedDevice_.isEmpty()) { filter_->hide(); catalog_->hide(); open_->hide(); }
    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(catalog_, &QTreeWidget::itemSelectionChanged, this, [this] { selectDevice(); });
}
void PeripheralPanel::updateTarget(const DebugTarget& debugger, unsigned core) {
    const bool changed = debugger_ != &debugger || chip_ != debugger.description().chip || hardware_ != debugger.hardware();
    hardware_ = debugger.hardware(); readOnce_->setVisible(hardware_); readOnce_->setEnabled(debugger.loaded() && !debugger.busy() && !debugger.executing());
    debugger_ = &debugger; core_ = core; chip_ = debugger.description().chip;
    if (changed) rebuildCatalog();
    context_->setText(QString("%1 · cœur %2 · révision %3")
        .arg(pinnedDevice_.isEmpty() ? QString("%1 blocs matériels").arg(static_cast<qulonglong>(devices_.size())) : pinnedDevice_)
        .arg(core_).arg(static_cast<qulonglong>(debugger.revision())));
    refreshValues();
}
void PeripheralPanel::rebuildCatalog() {
    const QSignalBlocker blocker(catalog_);
    inspector_->clear();
    devices_ = peripheralCatalog(chip_); catalog_->clear(); selected_ = -1;
    QTreeWidgetItem* initial = nullptr;
    for (size_t i = 0; i < devices_.size(); ++i) {
        const auto& device = devices_[i];
        auto* item = new QTreeWidgetItem(catalog_, {q(device.name), hardware_ ? (device.registers.empty() ? "Catalogue incomplet" : "Registres matériels") : q(device.coverage)});
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setToolTip(0, q(hex(device.base)) + (hardware_ ? " · Accès matériel via sonde" : q(" — " + device.note)));
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
void PeripheralPanel::refreshCatalogColors() {
    const auto supported = picoThemeColor("#8bd6bb");
    const auto unavailable = picoThemeColor("#e5ac74");
    for (int i = 0; i < catalog_->topLevelItemCount(); ++i) {
        auto* item = catalog_->topLevelItem(i);
        const auto& device = devices_.at(static_cast<size_t>(item->data(0, Qt::UserRole).toInt()));
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
    if (selected_ < 0) { detail_->clear(); context_->setToolTip({}); if (selectionChanged) selectionChanged({}); return; }
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    detail_->setText(q(device.name + " @ " + hex(device.base) + " — " + device.note) +
        (device.registers.empty() ? "\nRegistres non décrits dans cette version ; aucune valeur n'est inventée." : "\nLecture d'inspection sans effet de bord."));
    if (hardware_) detail_->setText(q(device.name + " @ " + hex(device.base)) +
        (device.registers.empty() ? " · Registres non décrits dans le catalogue actuel." : " · Valeurs réelles à l'arrêt. Les lectures sensibles nécessitent une action explicite ; aucun état simulé."));
    context_->setToolTip(detail_->text());
    detail_->setToolTip(detail_->text());
    registers_->setRowCount(static_cast<int>(device.registers.size()));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        const QString note = hardware_ ? QString("Description du catalogue ; valeur lue sur la carte") : q(reg.note);
        const QStringList values = {q(reg.name), "—", q(hex(reg.address)), q(reg.access), note};
        for (int col = 0; col < values.size(); ++col) {
            auto* value = new QTableWidgetItem(values[col]); value->setToolTip(note);
            registers_->setItem(static_cast<int>(i), col, value);
        }
    }
    filterRegisters(); refreshValues();
    if (selectionChanged) selectionChanged(selectedDevice());
}
void PeripheralPanel::focusDevice(const QString& name) {
    const bool general = name.isEmpty();
    filter_->setVisible(general); catalog_->setVisible(general); open_->setVisible(general);
    if (general) { applyFilter(); return; }
    filter_->clear(); registerFilter_->clear();
    const auto matches = catalog_->findItems(name, Qt::MatchExactly, 0);
    if (!matches.isEmpty()) catalog_->setCurrentItem(matches.front());
}
void PeripheralPanel::refreshValues() {
    if (!debugger_ || selected_ < 0) return;
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        auto* value = registers_->item(static_cast<int>(i), 1);
        auto* fields = registers_->item(static_cast<int>(i), 4);
        // A reset/profile change must not leave an old decoded field visible
        // alongside an unavailable register value.
        const QString note = hardware_ ? QString("Registre matériel ; certains accès modifient l'état de la carte") : q(reg.note);
        fields->setText(note);
        QString text, tooltip = note;
        bool numeric = false;
        uint32_t raw = 0;
        if (!debugger_->loaded()) text = hardware_ ? "Déconnectée" : "Pas d'image";
        else if (!hardware_ && device.coverage == "Non émulé") text = "Non émulé";
        else if (reg.access == "WO") text = "Écriture seule";
        else {
            try {
                raw = debugger_->peek(reg.address, 4, core_);
                text = q(hex(raw)); numeric = true;
                const auto decoded = fieldText(reg, raw);
                fields->setText(decoded.isEmpty() ? note : decoded);
                tooltip = note + "\n" + decoded;
            } catch (const Error& error) {
                tooltip = q(error.what());
                text = hardware_ && tooltip.startsWith("Lecture explicite") ? "Lecture explicite" : hardware_ && tooltip.contains("en cours") ? "Lecture…" : "Indisponible";
            }
        }
        const bool changed = numeric && value->data(Qt::UserRole).toBool() && value->text() != text;
        value->setForeground(picoThemeColor(changed ? "#ffd780" : numeric ? "#dce6f0" : "#e5ac74"));
        value->setText(text); value->setToolTip(tooltip); value->setData(Qt::UserRole, numeric);
        value->setData(Qt::UserRole + 1, numeric ? QVariant(raw) : QVariant());
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
    if (registers_->currentRow() < 0 || registers_->isRowHidden(registers_->currentRow()) || registers_->selectedItems().isEmpty()) {
        if (first >= 0) registers_->setCurrentCell(first, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        else { registers_->clearSelection(); inspector_->clear(); }
    }
    refreshRegisterDetails();
}
void PeripheralPanel::refreshRegisterDetails() {
    const int row = registers_->currentRow();
    if (selected_ < 0 || row < 0 || registers_->isRowHidden(row)) { inspector_->clear(); return; }
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    if (static_cast<size_t>(row) >= device.registers.size()) { inspector_->clear(); return; }
    auto* value = registers_->item(row, 1);
    if (!value) return;
    auto description = device.registers[static_cast<size_t>(row)];
    if (hardware_) description.note = "Registre matériel. Les lectures explicites peuvent modifier la cible ; les notes de simulation ne s'appliquent pas.";
    inspector_->setRegister(description);
    inspector_->setValue(value->data(Qt::UserRole).toBool(), value->data(Qt::UserRole + 1).toUInt(), value->text());
}
