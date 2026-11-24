#include "board_panel.hpp"
#include "picodbg/target.hpp"
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QPixmap drawLed(picodbg::LedState state, qreal scale) {
    using picodbg::LedState;
    QPixmap pixmap(qRound(32 * scale), qRound(22 * scale));
    pixmap.setDevicePixelRatio(scale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    if (state == LedState::On) {
        QRadialGradient glow(QPointF(16, 11), 15);
        glow.setColorAt(0, QColor(108, 248, 118, 180));
        glow.setColorAt(1, QColor(108, 248, 118, 0));
        painter.setBrush(glow);
        painter.drawEllipse(QRectF(0, 0, 32, 22));
    }
    painter.setBrush(QColor("#8c917f"));
    painter.drawRoundedRect(QRectF(5, 6, 22, 10), 2, 2);
    painter.setBrush(QColor("#2b322d"));
    painter.drawRoundedRect(QRectF(8, 5, 16, 12), 2, 2);
    QLinearGradient lens(0, 7, 0, 15);
    if (state == LedState::On) {
        lens.setColorAt(0, QColor("#eeffc3"));
        lens.setColorAt(1, QColor("#51e86a"));
    } else {
        lens.setColorAt(0, QColor(state == LedState::Off ? "#9da282" : "#918f93"));
        lens.setColorAt(1, QColor(state == LedState::Off ? "#666c51" : "#5c5a60"));
    }
    painter.setBrush(lens);
    painter.drawRoundedRect(QRectF(10, 7, 12, 8), 1.5, 1.5);
    return pixmap;
}
}

BoardLed::BoardLed(QWidget* parent) : QWidget(parent) {
    setObjectName("boardLed");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 2, 0);
    layout->setSpacing(1);
    lamp_ = new QLabel;
    lamp_->setFixedSize(32, 22);
    label_ = new QLabel;
    label_->setObjectName("boardLedState");
    layout->addWidget(lamp_);
    layout->addWidget(label_);
}

void BoardLed::updateTarget(const picodbg::DebugTarget& target) {
    using picodbg::LedState;
    setVisible(!target.hardware());
    if (target.hardware()) return;
    const auto state = target.onboardLed();
    const QString text = state == LedState::Unavailable ? "LED intégrée · état indisponible" :
        (state == LedState::On ? "LED intégrée · allumée" : "LED intégrée · éteinte");
    if (label_->text() == text) return;
    label_->setText(text);
    lamp_->setPixmap(drawLed(state, devicePixelRatioF()));
    setAccessibleName(text);
    setToolTip(state == LedState::Unavailable
        ? "LED de la carte W · WL_GPIO0 du CYW43. Le composant radio est hors du modèle d’émulation."
        : "LED de la carte · sortie GP25. L’état est actualisé après chaque tranche d’exécution.");
}

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
void BoardPanel::setBoard(const picodbg::BoardDescription& board, bool hardware) {
    summary_->setText(QString::fromStdString(board.name) + QString(" · %1 · Flash %2 Mio\n")
        .arg(board.chip == picodbg::Chip::RP2040 ? "RP2040" : "RP2350 ARM")
        .arg(board.flashSize / (1024u * 1024u)) +
        (board.wireless ? "Radio et LED CYW43 non émulées. Adresses MMIO identiques à la carte sans Wi-Fi de la même famille."
                        : "LED sur GPIO25. Le voyant de la barre d’état affiche le niveau de sortie de la broche.") +
        "\nChoisir un ELF pour cette carte : la compatibilité du câblage n'est pas détectable dans tout ELF.");
    if (hardware) summary_->setText(QString::fromStdString(board.name) + " · carte sélectionnée · accès réel par SWD\n"
        "Le câblage W n'est pas déduit de l'identité du microcontrôleur. CYW43 est externe au SoC : ses registres ne sont pas accessibles directement par SWD.");
    wiring_->setRowCount(static_cast<int>(board.connections.size()));
    for (size_t i = 0; i < board.connections.size(); ++i) {
        const auto& connection = board.connections[i];
        const QStringList values = {QString::fromStdString(connection.function), QString::fromStdString(connection.connection), hardware ? QString("Connexion physique de la carte ; visibilité selon le registre et le serveur") : QString::fromStdString(connection.note)};
        for (int col = 0; col < values.size(); ++col) {
            auto* item = new QTableWidgetItem(values[col]); item->setToolTip(values[col]);
            wiring_->setItem(static_cast<int>(i), col, item);
        }
    }
    wiring_->resizeRowsToContents();
}
