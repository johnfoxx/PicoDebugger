#include "board_panel.hpp"
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

BoardPanel::BoardPanel(QWidget* parent) : QWidget(parent) {
    setObjectName("boardPanel");
    auto* layout = new QVBoxLayout(this);
    summary_ = new QLabel; summary_->setObjectName("boardSummary"); summary_->setWordWrap(true);
    wiring_ = new QTableWidget(0, 3); wiring_->setObjectName("boardWiringTable");
    wiring_->setHorizontalHeaderLabels({"Fonction", "Connexion sur la carte", "Couverture / remarque"});
    wiring_->setEditTriggers(QAbstractItemView::NoEditTriggers); wiring_->setAlternatingRowColors(true);
    wiring_->verticalHeader()->hide();
    wiring_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    wiring_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    // The stretched description column gets its final width when the tab is
    // shown/resized. Recompute wrapping then, not only before layout exists.
    auto* resizeRows = new QTimer(this); resizeRows->setSingleShot(true);
    connect(resizeRows, &QTimer::timeout, wiring_, &QTableWidget::resizeRowsToContents);
    connect(wiring_->horizontalHeader(), &QHeaderView::sectionResized, resizeRows, [resizeRows] { resizeRows->start(0); });
    layout->addWidget(summary_); layout->addWidget(wiring_, 1);
}
void BoardPanel::setBoard(const picodbg::BoardDescription& board) {
    summary_->setText(QString::fromStdString(board.name) + QString(" · %1 · Flash %2 Mio\n")
        .arg(board.chip == picodbg::Chip::RP2040 ? "RP2040" : "RP2350 ARM")
        .arg(board.flashSize / (1024u * 1024u)) +
        (board.wireless ? "Radio et LED CYW43 non émulées. Adresses MMIO identiques à la carte sans Wi-Fi de la même famille."
                        : "LED sur GPIO25. Les connexions ci-dessous décrivent la carte ; les signaux électriques ne sont pas simulés.") +
        "\nChoisir un ELF pour cette carte : la compatibilité du câblage n'est pas détectable dans tout ELF.");
    wiring_->setRowCount(static_cast<int>(board.connections.size()));
    for (size_t i = 0; i < board.connections.size(); ++i) {
        const auto& connection = board.connections[i];
        const QStringList values = {QString::fromStdString(connection.function), QString::fromStdString(connection.connection), QString::fromStdString(connection.note)};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]); item->setToolTip(values[col]);
            wiring_->setItem(static_cast<int>(i), col, item);
        }
    }
    wiring_->resizeRowsToContents();
}
