#include "virtual_time_panel.hpp"
#include "choice_box.hpp"
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

VirtualTimePanel::VirtualTimePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    clock_ = new QLabel; clock_->setObjectName("virtualTimeValue"); layout->addWidget(clock_);
    auto* controls = new QHBoxLayout;
    duration_ = new QSpinBox; duration_->setObjectName("virtualTimeDuration"); duration_->setRange(1, 1000000); duration_->setValue(1);
    unit_ = new ChoiceBox; unit_->setObjectName("virtualTimeUnit"); unit_->addItems({"µs", "ms"}); unit_->setCurrentIndex(1);
    mode_ = new ChoiceBox; mode_->setObjectName("virtualTimeMode");
    mode_->addItems({"Exécuter le firmware", "CPU figés · périphériques seuls"});
    advance_ = new QPushButton("Avancer"); advance_->setObjectName("advanceVirtualTimeButton");
    pause_ = new QPushButton("Pause"); pause_->setObjectName("pauseVirtualTimeButton");
    controls->addWidget(duration_); controls->addWidget(unit_); controls->addWidget(mode_, 1);
    controls->addWidget(advance_); controls->addWidget(pause_); layout->addLayout(controls);
    auto* help = new QLabel("Temps depuis le chargement/reset. Un quantum exécuté = 1 µs virtuelle, sans précision de cycles matériels.\n"
        "Firmware : les points d'arrêt, fautes et attentes peuvent interrompre la durée. CPU figés : timers, UART, SPI, PWM et SysTick avancent ; les IRQ attendent la reprise des CPU.");
    help->setWordWrap(true); layout->addWidget(help); layout->addStretch();
    connect(advance_, &QPushButton::clicked, this, [this] {
        if (advanceRequested) advanceRequested(static_cast<uint64_t>(duration_->value()) * (unit_->currentIndex() == 0 ? 1u : 1000u), mode_->currentIndex() == 1);
    });
    connect(pause_, &QPushButton::clicked, this, [this] { if (pauseRequested) pauseRequested(); });
}
void VirtualTimePanel::updateState(uint64_t time, uint64_t remaining, bool available, bool running) {
    clock_->setText(QString("t = %1 µs (%2 ms)%3").arg(static_cast<qulonglong>(time))
        .arg(static_cast<double>(time) / 1000.0, 0, 'f', 3)
        .arg(remaining ? QString(" · reste %1 µs").arg(static_cast<qulonglong>(remaining)) : QString()));
    advance_->setEnabled(available && !running); pause_->setEnabled(running);
    duration_->setEnabled(!running); unit_->setEnabled(!running); mode_->setEnabled(!running);
}
