#include "peripheral_inspection.hpp"
#include "choice_box.hpp"
#include "peripheral_panel.hpp"
#include "uart_terminal.hpp"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QTabWidget>
#include <QVBoxLayout>

PeripheralInspection::PeripheralInspection(const QString& name, unsigned core, QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("deviceInspection_" + name);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 6);
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(name); title->setObjectName("sectionTitle"); header->addWidget(title);
    header->addStretch();
    core_ = new ChoiceBox; core_->setObjectName("inspectionCore");
    core_->addItems({"Contexte : cœur 0", "Contexte : cœur 1"}); core_->setCurrentIndex(static_cast<int>(core));
    header->addWidget(core_);
    detach_ = new QPushButton; detach_->setObjectName("detachInspectionButton"); setDetached(false);
    header->addWidget(detach_);
    auto* more = new QToolButton; more->setText("⋯"); more->setAccessibleName("Options de l'inspecteur");
    more->setToolTip("Options de l'inspecteur"); more->setPopupMode(QToolButton::InstantPopup);
    auto* options = new QMenu(more);
    auto* duplicate = options->addAction("Dupliquer l'inspection"); duplicate->setObjectName("duplicateInspectionAction");
    connect(duplicate, &QAction::triggered, this, [this] { if (duplicateRequested) duplicateRequested(); });
    more->setMenu(options); header->addWidget(more); layout->addLayout(header);
    panel_ = new PeripheralPanel(this, name);
    uart_ = name == "UART0" ? 0 : name == "UART1" ? 1 : -1;
    if (uart_ >= 0) {
        auto* tabs = new QTabWidget; tabs->setObjectName("uartTabs"); tabs->setDocumentMode(true);
        tabs->addTab(panel_, "Registres");
        terminal_ = new UartTerminal(static_cast<unsigned>(uart_));
        terminal_->injectUart = [this](unsigned uart, const std::string& bytes) {
            return injectUart ? injectUart(uart, bytes) : size_t(0);
        };
        tabs->addTab(terminal_, "UART · TX / RX"); layout->addWidget(tabs, 1);
    } else layout->addWidget(panel_, 1);
    connect(core_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this] { if (refreshRequested) refreshRequested(); });
    connect(detach_, &QPushButton::clicked, this, [this] { if (detachRequested) detachRequested(); });
}
void PeripheralInspection::setDetached(bool detached) {
    detach_->setText(detached ? "Rattacher" : "Détacher");
    detach_->setToolTip(detached ? "Revenir dans l'application ; fermer cette fenêtre a le même effet"
                                : "Déplacer cet inspecteur dans une fenêtre indépendante");
}
unsigned PeripheralInspection::core() const { return static_cast<unsigned>(core_->currentIndex()); }
void PeripheralInspection::updateTarget(const picodbg::Debugger& debugger) {
    panel_->updateTarget(debugger, static_cast<unsigned>(core_->currentIndex()));
    if (terminal_) terminal_->updateTarget(debugger);
}
