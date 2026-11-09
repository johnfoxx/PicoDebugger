#include "peripheral_panel.hpp"
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
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
PeripheralPanel::PeripheralPanel(QWidget* parent, const QString& pinnedDevice) : QWidget(parent), pinnedDevice_(pinnedDevice) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4);
    filter_ = new QLineEdit; filter_->setObjectName("peripheralFilter");
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
    detail_ = new QLabel; detail_->setWordWrap(true); detail_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* split = new QSplitter(Qt::Vertical); split->addWidget(catalog_); split->addWidget(registers_);
    split->setSizes({120, 310});
    auto* open = new QPushButton("Ouvrir dans une petite fenêtre"); open->setObjectName("openDeviceWindowButton");
    layout->addWidget(filter_); layout->addWidget(open); layout->addWidget(context_); layout->addWidget(split, 1); layout->addWidget(detail_);
    connect(open, &QPushButton::clicked, this, [this] { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    connect(catalog_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*, int) { if (openDevice && selected_ >= 0) openDevice(selectedDevice()); });
    if (!pinnedDevice_.isEmpty()) { filter_->hide(); catalog_->hide(); open->hide(); }
    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(catalog_, &QTreeWidget::itemSelectionChanged, this, [this] { selectDevice(); });
}
void PeripheralPanel::updateTarget(const Debugger& debugger, unsigned core) {
    const bool changed = debugger_ != &debugger || chip_ != debugger.memory.description().chip;
    debugger_ = &debugger; core_ = core; chip_ = debugger.memory.description().chip;
    if (changed) rebuildCatalog();
    context_->setText(QString("%1 blocs • cœur %2 • état figé à la révision %3")
        .arg(static_cast<qulonglong>(devices_.size())).arg(core_).arg(static_cast<qulonglong>(debugger.revision())));
    refreshValues();
}
void PeripheralPanel::rebuildCatalog() {
    const QSignalBlocker blocker(catalog_);
    devices_ = peripheralCatalog(chip_); catalog_->clear(); selected_ = -1;
    QTreeWidgetItem* initial = nullptr;
    for (size_t i = 0; i < devices_.size(); ++i) {
        const auto& device = devices_[i];
        auto* item = new QTreeWidgetItem(catalog_, {q(device.name), q(device.coverage)});
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        item->setToolTip(0, q(hex(device.base) + " — " + device.note));
        item->setForeground(1, QColor(device.coverage == "Non émulé" ? "#e5ac74" : "#8bd6bb"));
        if (q(device.name) == (pinnedDevice_.isEmpty() ? QString("SIO") : pinnedDevice_)) initial = item;
    }
    if (initial) catalog_->setCurrentItem(initial);
    applyFilter(); selectDevice();
}
QString PeripheralPanel::selectedDevice() const {
    return selected_ < 0 ? QString() : q(devices_.at(static_cast<size_t>(selected_)).name);
}
void PeripheralPanel::applyFilter() {
    const auto needle = filter_->text().trimmed();
    for (int i = 0; i < catalog_->topLevelItemCount(); ++i) {
        auto* item = catalog_->topLevelItem(i);
        const auto& device = devices_.at(static_cast<size_t>(item->data(0, Qt::UserRole).toInt()));
        QString searchable = q(device.name + " " + device.coverage + " " + hex(device.base));
        for (const auto& reg : device.registers) searchable += " " + q(reg.name + " " + hex(reg.address));
        item->setHidden(!searchable.contains(needle, Qt::CaseInsensitive));
    }
}
void PeripheralPanel::selectDevice() {
    auto* item = catalog_->currentItem();
    selected_ = item ? item->data(0, Qt::UserRole).toInt() : -1;
    registers_->setRowCount(0);
    if (selected_ < 0) { detail_->clear(); return; }
    const auto& device = devices_.at(static_cast<size_t>(selected_));
    detail_->setText(q(device.name + " @ " + hex(device.base) + " — " + device.note) +
        (device.registers.empty() ? "\nRegistres non décrits dans cette version ; aucune valeur n'est inventée." : "\nLecture d'inspection sans effet de bord."));
    registers_->setRowCount(static_cast<int>(device.registers.size()));
    for (size_t i = 0; i < device.registers.size(); ++i) {
        const auto& reg = device.registers[i];
        const QStringList values = {q(reg.name), "—", q(hex(reg.address)), q(reg.access), q(reg.note)};
        for (int col = 0; col < values.size(); ++col) {
            auto* value = new QTableWidgetItem(values[col]); value->setToolTip(q(reg.note));
            registers_->setItem(static_cast<int>(i), col, value);
        }
    }
    refreshValues();
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
        fields->setText(q(reg.note));
        QString text, tooltip = q(reg.note);
        bool numeric = false;
        if (!debugger_->loaded()) text = "Pas d'image";
        else if (device.coverage == "Non émulé") text = "Non émulé";
        else if (reg.access == "WO") text = "Écriture seule";
        else {
            try {
                const uint32_t raw = debugger_->memory.peek(reg.address, 4, core_);
                text = q(hex(raw)); numeric = true;
                const auto decoded = fieldText(reg, raw);
                fields->setText(decoded.isEmpty() ? q(reg.note) : decoded);
                tooltip = q(reg.note) + "\n" + decoded;
            } catch (const Error& error) { text = "Indisponible"; tooltip = q(error.what()); }
        }
        const bool changed = numeric && value->data(Qt::UserRole).toBool() && value->text() != text;
        value->setForeground(QColor(changed ? "#ffd780" : numeric ? "#dce6f0" : "#e5ac74"));
        value->setText(text); value->setToolTip(tooltip); value->setData(Qt::UserRole, numeric);
    }
}
