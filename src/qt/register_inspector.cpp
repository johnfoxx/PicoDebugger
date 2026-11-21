#include "register_inspector.hpp"
#include "window_theme.hpp"
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
}
RegisterInspector::RegisterInspector(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    title_ = new QLabel; title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title_->setWordWrap(true); title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    note_ = new QLabel; note_->setWordWrap(true); note_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    note_->setMaximumHeight(48);
    fields_ = new QTableWidget(0, 4); fields_->setObjectName("registerFieldsTable");
    fields_->setHorizontalHeaderLabels({"Bits", "Champ", "Valeur", "Signification"});
    fields_->verticalHeader()->hide(); fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fields_->setAlternatingRowColors(true); fields_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fields_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    fields_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(title_); layout->addWidget(fields_, 1); layout->addWidget(note_);
    clear();
}
void RegisterInspector::clear() {
    selected_ = false; fields_->setRowCount(0); note_->clear();
    title_->setText("Sélectionner un registre pour examiner ses bits.");
}
void RegisterInspector::setRegister(const picodbg::RegisterDescription& description) {
    if (selected_ && register_.address == description.address && register_.name == description.name) return;
    register_ = description; selected_ = true;
    fields_->setRowCount(0); fields_->setRowCount(static_cast<int>(register_.fields.size()));
    for (size_t i = 0; i < register_.fields.size(); ++i) {
        const auto& field = register_.fields[i];
        const auto bits = field.width == 1 ? QString::number(field.lsb)
            : QString("%1:%2").arg(field.lsb + field.width - 1).arg(field.lsb);
        QString help = q(field.description);
        for (const auto& meaning : field.meanings) help += QString("\n%1 : %2").arg(meaning.value).arg(q(meaning.text));
        const QStringList values = {bits, q(field.name), "—", q(field.description)};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]); item->setToolTip(help);
            fields_->setItem(static_cast<int>(i), col, item);
        }
    }
    note_->setText(q(register_.note) + (register_.fields.empty() ? "\nChamps pas encore décrits." : "\nLes bits absents de ce tableau ne sont pas nécessairement réservés."));
    note_->setToolTip(note_->text());
}
void RegisterInspector::setValue(bool available, uint32_t value, const QString& status) {
    if (!selected_) return;
    title_->setText(q(register_.name + " @ " + picodbg::hex(register_.address) + " · " + register_.access) + " · " + status);
    for (size_t i = 0; i < register_.fields.size(); ++i) {
        const auto& field = register_.fields[i];
        auto* raw = fields_->item(static_cast<int>(i), 2);
        const auto text = available ? QString::number(picodbg::fieldValue(value, field)) : QString("—");
        const bool changed = available && raw->data(Qt::UserRole).toBool() && raw->text() != text;
        raw->setText(text); raw->setData(Qt::UserRole, available);
        raw->setForeground(picoThemeColor(changed ? "#ffd780" : available ? "#dce6f0" : "#e5ac74"));
        QString meaning = q(field.description);
        if (available && !field.meanings.empty()) meaning += " : " + q(picodbg::fieldMeaning(value, field));
        fields_->item(static_cast<int>(i), 3)->setText(meaning);
    }
    fields_->resizeRowsToContents();
}
