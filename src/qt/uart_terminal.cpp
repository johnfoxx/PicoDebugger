#include "uart_terminal.hpp"
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

UartTerminal::UartTerminal(unsigned uart, QWidget* parent) : QWidget(parent), uart_(uart) {
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Sortie TX · derniers octets émis"));
    output_ = new QPlainTextEdit; output_->setObjectName("uartOutput"); output_->setReadOnly(true);
    output_->setPlaceholderText("Les octets émis par le firmware apparaîtront ici.");
    layout->addWidget(output_, 1);
    auto* inputRow = new QHBoxLayout;
    auto* input = new QLineEdit; input->setObjectName("uartInput");
    input->setPlaceholderText("Texte RX (UTF-8, sans retour ligne ajouté)");
    auto* send = new QPushButton("Injecter RX"); send->setObjectName("uartSend");
    inputRow->addWidget(input, 1); inputRow->addWidget(send); layout->addLayout(inputRow);
    auto* result = new QLabel; result->setObjectName("uartInputResult"); result->setWordWrap(true); layout->addWidget(result);
    connect(send, &QPushButton::clicked, this, [this, input, result] {
        if (!injectUart) return;
        try {
            const auto bytes = input->text().toUtf8();
            const auto accepted = injectUart(uart_, std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
            result->setText(QString("%1 / %2 octets acceptés · exécution mise en pause")
                .arg(static_cast<qulonglong>(accepted)).arg(bytes.size()));
        } catch (const std::exception& error) { result->setText(QString::fromUtf8(error.what())); }
    });
    connect(input, &QLineEdit::returnPressed, send, &QPushButton::click);
}
void UartTerminal::updateTarget(const picodbg::DebugTarget& debugger) {
    if (debugger.hardware()) {
        output_->setPlainText("La sonde SWD ne capture pas l'UART. Utiliser le port série USB de la Debug Probe, relié aux broches UART de la carte.\nL'injection RX simulée n'est pas disponible en mode réel.");
        findChild<QPushButton*>("uartSend")->setEnabled(false); return;
    }
    const auto bytes = debugger.uartOutput(uart_);
    const auto text = QString::fromLatin1(bytes.data(), static_cast<int>(bytes.size()));
    if (output_->toPlainText() != text) output_->setPlainText(text);
    findChild<QPushButton*>("uartSend")->setEnabled(debugger.loaded() && !debugger.terminalStop());
}
