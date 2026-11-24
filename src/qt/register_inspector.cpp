#include "register_inspector.hpp"
#include "debug_access.hpp"
#include "debug_value.hpp"
#include "window_theme.hpp"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }
}
RegisterInspector::RegisterInspector(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    title_ = new QLabel; title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    fields_ = new QTableWidget(0, 4); fields_->setObjectName("registerFieldsTable");
    fields_->setHorizontalHeaderLabels({"Bits", "Champ", "Valeur", "Signification"});
    fields_->verticalHeader()->hide(); fields_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fields_->setAlternatingRowColors(true); fields_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fields_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    fields_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    resizeRows_ = new QTimer(this);
    resizeRows_->setSingleShot(true);
    connect(resizeRows_, &QTimer::timeout, fields_, &QTableWidget::resizeRowsToContents);
    connect(fields_->horizontalHeader(), &QHeaderView::sectionResized, this, [this] { resizeRows_->start(0); });
    auto* header = new QHBoxLayout;
    header->addWidget(title_, 1);
    edit_ = new QPushButton("Modifier…");
    edit_->setObjectName("editRegisterFieldButton");
    header->addWidget(edit_);
    layout->addLayout(header); layout->addWidget(fields_, 1);
    connect(fields_, &QTableWidget::currentCellChanged, this, [this] { updateEditor(); });
    connect(fields_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == 2 && row == fields_->currentRow() && edit_->isEnabled()) edit_->click();
    });
    connect(edit_, &QPushButton::clicked, this, [this] {
        const int row = fields_->currentRow();
        if (editField && row >= 0 && static_cast<size_t>(row) < register_.fields.size()) editField(register_.fields[static_cast<size_t>(row)]);
    });
    clear();
}
void RegisterInspector::clear() {
    selected_ = false; fields_->setRowCount(0);
    title_->setText("Sélectionner un registre pour examiner ses bits.");
    title_->setToolTip({});
    updateEditor();
}
void RegisterInspector::setRegister(const picodbg::RegisterDescription& description) {
    if (selected_ && register_.address == description.address && register_.name == description.name) return;
    register_ = description; selected_ = true;
    std::stable_sort(register_.fields.begin(), register_.fields.end(),
                     [](const picodbg::RegisterField& a, const picodbg::RegisterField& b) { return a.lsb < b.lsb; });
    if (register_.fields.empty()) {
        for (unsigned bit = 0; bit < 32; ++bit)
            register_.fields.emplace_back("—", bit, 1, "Fonction non documentée dans le catalogue");
    }
    fields_->setRowCount(0); fields_->setRowCount(static_cast<int>(register_.fields.size()));
    for (size_t i = 0; i < register_.fields.size(); ++i) {
        const auto& field = register_.fields[i];
        const auto bits = field.width == 1 ? QString::number(field.lsb)
            : QString("%1:%2").arg(field.lsb + field.width - 1).arg(field.lsb);
        const auto descriptionText = field.description.empty() ? QString("Description non renseignée") : q(field.description);
        QString help = descriptionText;
        for (const auto& meaning : field.meanings) help += QString("\n%1 — %2").arg(meaning.value).arg(q(meaning.text));
        const QStringList values = {bits, q(field.name), "—", descriptionText};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]); item->setToolTip(help);
            if (col == 0 || col == 2) item->setTextAlignment(Qt::AlignCenter);
            fields_->setItem(static_cast<int>(i), col, item);
        }
    }
    fields_->scrollToTop();
    resizeRows_->start(0);
}
void RegisterInspector::setValue(bool available, uint32_t value, const QString& status, bool writable) {
    if (!selected_) return;
    available_ = available;
    writable_ = writable;
    const auto address = q(register_.name + " @ " + picodbg::hex(register_.address));
    title_->setText(QString("%1 · <b style=\"color:%2\">%3</b> · %4")
        .arg(q(register_.name).toHtmlEscaped(), debugAccessColor(registerAccess(register_)).name(),
             q(register_.access).toHtmlEscaped(), status.toHtmlEscaped()));
    title_->setToolTip(address + "\n" + status + "\n" + registerAccessText(register_) + "\n" + q(register_.note));
    bool meaningChanged = false;
    for (size_t i = 0; i < register_.fields.size(); ++i) {
        const auto& field = register_.fields[i];
        auto* raw = fields_->item(static_cast<int>(i), 2);
        const auto text = available ? QString::number(picodbg::fieldValue(value, field)) : QString("—");
        const bool changed = available && raw->data(Qt::UserRole).toBool() && raw->text() != text;
        raw->setText(text); raw->setData(Qt::UserRole, available);
        raw->setForeground(picoThemeColor(changed ? "#ffd780" : available ? "#dce6f0" : "#e5ac74"));
        if (!field.meanings.empty()) {
            auto meaning = q(field.description);
            if (available) {
                if (!meaning.isEmpty()) meaning += " · ";
                meaning += q(picodbg::fieldMeaning(value, field));
            }
            auto* cell = fields_->item(static_cast<int>(i), 3);
            if (cell->text() == meaning) continue;
            cell->setText(meaning);
            meaningChanged = true;
        }
    }
    if (meaningChanged) resizeRows_->start(0);
    updateEditor();
}
void RegisterInspector::updateEditor() {
    const int row = fields_->currentRow();
    auto mode = picodbg::FieldWriteMode::Unavailable;
    if (selected_ && writable_ && row >= 0 && static_cast<size_t>(row) < register_.fields.size())
        mode = picodbg::fieldWriteMode(register_, register_.fields[static_cast<size_t>(row)]);
    const bool clear = mode == picodbg::FieldWriteMode::Clear && register_.access == "W1C";
    edit_->setEnabled(mode != picodbg::FieldWriteMode::Unavailable && (available_ || clear));
    edit_->setText(mode == picodbg::FieldWriteMode::Clear ? "Effacer…" : "Modifier…");
    edit_->setToolTip(edit_->isEnabled() ? "Écrire le champ sélectionné sur la cible"
                                        : "Sélectionner un champ modifiable avec la cible arrêtée");
}
