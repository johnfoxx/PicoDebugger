#include "peripheral_inspection.hpp"
#include "choice_box.hpp"
#include "peripheral_panel.hpp"
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
        auto* terminal = new QWidget; auto* serialLayout = new QVBoxLayout(terminal);
        serialLayout->addWidget(new QLabel("Sortie TX · derniers octets émis"));
        output_ = new QPlainTextEdit; output_->setObjectName("uartOutput"); output_->setReadOnly(true);
        output_->setPlaceholderText("Les octets émis par le firmware apparaîtront ici.");
        serialLayout->addWidget(output_, 1);
        auto* inputRow = new QHBoxLayout;
        auto* input = new QLineEdit; input->setObjectName("uartInput");
        input->setPlaceholderText("Texte RX (UTF-8, sans retour ligne ajouté)");
        auto* send = new QPushButton("Injecter RX"); send->setObjectName("uartSend");
        inputRow->addWidget(input, 1); inputRow->addWidget(send); serialLayout->addLayout(inputRow);
        auto* result = new QLabel; result->setObjectName("uartInputResult"); result->setWordWrap(true); serialLayout->addWidget(result);
        tabs->addTab(terminal, "UART · TX / RX"); layout->addWidget(tabs, 1);
        connect(send, &QPushButton::clicked, this, [this, input, result] {
            if (!injectUart) return;
            try {
                const auto bytes = input->text().toUtf8();
                const auto accepted = injectUart(static_cast<unsigned>(uart_), std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
                result->setText(QString("%1 / %2 octets acceptés · exécution mise en pause")
                    .arg(static_cast<qulonglong>(accepted)).arg(bytes.size()));
            } catch (const std::exception& error) { result->setText(QString::fromUtf8(error.what())); }
        });
        connect(input, &QLineEdit::returnPressed, send, &QPushButton::click);
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
    if (output_) {
        const auto& bytes = debugger.memory.uartOutput(static_cast<unsigned>(uart_));
        const auto text = QString::fromLatin1(bytes.data(), static_cast<int>(bytes.size()));
        if (output_->toPlainText() != text) output_->setPlainText(text);
    }
}
