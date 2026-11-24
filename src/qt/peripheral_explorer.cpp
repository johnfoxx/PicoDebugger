#include "peripheral_explorer.hpp"
#include "board_panel.hpp"
#include "choice_box.hpp"
#include "peripheral_panel.hpp"
#include "svd_catalog.hpp"
#include "uart_terminal.hpp"
#include <QComboBox>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

PeripheralExplorer::PeripheralExplorer(const picodbg::DebugTarget& debugger, unsigned core, QWidget* parent)
    : QDialog(parent, Qt::Window) {
    setObjectName("peripheralExplorer"); setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Périphériques — " + QString::fromStdString(debugger.board().name)); resize(1120, 760);
    auto* layout = new QVBoxLayout(this);
    auto* header = new QHBoxLayout;
    view_ = new ChoiceBox; view_->setObjectName("peripheralExplorerView");
    view_->addItem("Vue générale", QString());
    for (const auto& device : picosvd::catalog(debugger.description().chip))
        view_->addItem(QString::fromStdString(device.name), QString::fromStdString(device.name));
    if (debugger.board().wireless) view_->addItem(debugger.hardware() ? "CYW43 · externe, hors accès SWD direct" : "CYW43 · radio (non émulée)", "@CYW43");
    core_ = new ChoiceBox; core_->setObjectName("peripheralExplorerCore");
    core_->addItems({"Contexte : cœur 0", "Contexte : cœur 1"}); core_->setCurrentIndex(static_cast<int>(core));
    header->addWidget(new QLabel("Vue")); header->addWidget(view_, 1); header->addWidget(core_); layout->addLayout(header);
    snapshot_ = new QLabel; snapshot_->setObjectName("peripheralSnapshot"); layout->addWidget(snapshot_);
    tabs_ = new QTabWidget; tabs_->setObjectName("peripheralExplorerTabs");
    panel_ = new PeripheralPanel(nullptr, {}, true);
    tabs_->addTab(panel_, "Registres et bits");
    for (unsigned i = 0; i < 2; ++i) {
        terminals_[i] = new UartTerminal(i);
        terminals_[i]->injectUart = [this](unsigned uart, const std::string& bytes) {
            return injectUart ? injectUart(uart, bytes) : size_t(0);
        };
        tabs_->addTab(terminals_[i], QString("UART%1 · TX / RX").arg(i));
        tabs_->setTabVisible(static_cast<int>(i + 1), false);
    }
    auto* board = new BoardPanel; board->setBoard(debugger.board(), debugger.hardware());
    tabs_->addTab(board, "Carte et câblage"); layout->addWidget(tabs_, 1);
    panel_->selectionChanged = [this](const QString& name) {
        for (int i = 0; i < 2; ++i) tabs_->setTabVisible(i + 1, name == QString("UART%1").arg(i));
    };
    panel_->openDevice = [this](const QString& name) { view_->setCurrentIndex(view_->findData(name)); };
    connect(view_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this] {
        const auto device = view_->currentData().toString();
        if (device == "@CYW43") tabs_->setCurrentIndex(3);
        else { panel_->focusDevice(device); tabs_->setCurrentIndex(0); }
        if (refreshRequested) refreshRequested();
    });
    connect(core_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this] { if (refreshRequested) refreshRequested(); });
    updateTarget(debugger);
}
void PeripheralExplorer::updateTarget(const picodbg::DebugTarget& debugger, bool running) {
    panel_->updateTarget(debugger, static_cast<unsigned>(core_->currentIndex()), running);
    for (auto* terminal : terminals_) terminal->updateTarget(debugger);
    if (debugger.hardware()) {
        snapshot_->setText(debugger.executing() ? "Carte réelle en exécution · arrêter pour inspecter" : "Lectures matérielles à l'arrêt · périphériques non figés · certaines lectures ont des effets de bord");
        return;
    }
    snapshot_->setText(QString("%1 · t = %2 µs · révision %3 · inspection sans effet de bord")
        .arg(QString::fromStdString(debugger.board().name)).arg(static_cast<qulonglong>(debugger.virtualTime()))
        .arg(static_cast<qulonglong>(debugger.revision())));
}
