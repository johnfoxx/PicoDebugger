#include "main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> T* find(QObject& object, const char* name) {
    auto* result = object.findChild<T*>(name);
    if (!result) throw std::runtime_error(std::string("Missing widget: ") + name);
    return result;
}
void waitUntil(const std::function<bool()>& done) {
    QElapsedTimer timeout; timeout.start();
    do { QApplication::processEvents(); if (done()) return; QThread::msleep(5); } while (timeout.elapsed() < 5000);
    throw std::runtime_error("UI operation timed out");
}
void choose(QComboBox* choice, const QString& data) {
    const int index = choice->findData(data); check(index >= 0, "Missing peripheral choice"); choice->setCurrentIndex(index);
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setOrganizationName("PicoDebuggerTests"); QApplication::setApplicationName("PicoDebuggerSessionTests");
    try {
        MainWindow window; window.resize(1280, 800); window.show(); window.loadDemo(); QApplication::processEvents();
        auto* workspace = find<QTabWidget>(window, "inspectionWorkspace");
        auto* assembly = find<QTableWidget>(window, "assemblyTable");
        auto* registers = find<QTableWidget>(window, "registersTable");
        auto* open = find<QAction>(window, "peripheralExplorerAction"); open->trigger(); QApplication::processEvents();
        QPointer<QDialog> explorer = find<QDialog>(window, "peripheralExplorer");
        auto* view = find<QComboBox>(*explorer, "peripheralExplorerView");
        auto* values = find<QTableWidget>(*explorer, "peripheralsTable");
        auto* bits = find<QTableWidget>(*explorer, "registerFieldsTable");
        check(explorer->isVisible() && assembly->isVisible() && workspace->currentIndex() == 0, "Independent window must keep assembly visible");
        open->trigger(); check(window.findChildren<QDialog*>("peripheralExplorer").size() == 1, "Repeated open must reuse the independent window");
        choose(view, "SIO"); find<QComboBox>(*explorer, "peripheralExplorerCore")->setCurrentIndex(1);
        check(values->item(0, 1)->text() == "0x00000001", "Explorer core context");
        check(find<QComboBox>(window, "coreSelector")->currentIndex() == 0, "Explorer must preserve main CPU selection");
        check(!find<QTreeWidget>(*explorer, "peripheralCatalog")->isVisible(), "Focused view must dedicate space to registers");
        choose(view, "TIMER");
        find<QLineEdit>(*explorer, "registerFilter")->setText("TIMERAWL");
        find<QAction>(window, "stepAction")->trigger();
        check(values->item(10, 1)->text() == "0x00000001", "Separate window must follow execution");
        check(values->isVisible() && bits->isVisible(), "Registers and decoded bits must coexist");
        check(find<QLineEdit>(*explorer, "registerFilter")->text() == "TIMERAWL", "Refreshing must preserve the filter");
        if (argc > 1) check(explorer->grab().save(QString::fromLocal8Bit(argv[1]) + ".timer.png"), "Save detailed timer");
        choose(view, "");
        check(find<QTreeWidget>(*explorer, "peripheralCatalog")->isVisible(), "General view must restore catalogue");
        find<QLineEdit>(*explorer, "peripheralFilter")->setText("no_such_device");
        check(values->rowCount() == 0 && bits->rowCount() == 0, "Empty general search clears stale details");
        choose(view, "UART0");
        check(values->rowCount() == 13 && bits->rowCount() > 0, "Focusing must recover from an empty catalogue filter");
        auto* explorerTabs = find<QTabWidget>(*explorer, "peripheralExplorerTabs");
        check(explorerTabs->isTabVisible(1), "UART terminal must be available");
        explorerTabs->setCurrentIndex(1);
        find<QLineEdit>(*explorerTabs->widget(1), "uartInput")->setText("preserved");
        choose(view, "SIO"); choose(view, "UART0");
        check(find<QLineEdit>(*explorerTabs->widget(1), "uartInput")->text() == "preserved", "Switching peripheral must preserve unsent UART text");
        if (argc > 1) check(explorer->grab().save(QString::fromLocal8Bit(argv[1]) + ".uart-registers.png"), "Save UART detail view");
        // Manual time and exact-duration execution share the same live views.
        find<QAction>(window, "resetAction")->trigger(); choose(view, "TIMER");
        find<QAction>(window, "virtualTimeViewAction")->trigger();
        auto* duration = find<QSpinBox>(window, "virtualTimeDuration");
        auto* unit = find<QComboBox>(window, "virtualTimeUnit");
        auto* mode = find<QComboBox>(window, "virtualTimeMode");
        auto* advance = find<QPushButton>(window, "advanceVirtualTimeButton");
        auto* clock = find<QLabel>(window, "virtualTimeValue");
        duration->setValue(25); unit->setCurrentIndex(0); mode->setCurrentIndex(1); advance->click();
        waitUntil([&] { return advance->isEnabled(); });
        check(clock->text().startsWith("t = 25 µs") && values->item(10, 1)->text() == "0x00000019", "Manual advance updates virtual clock and peripheral window");
        check(assembly->item(0, 1)->text() == "0x10000200" && registers->item(0, 1)->text() == "0x00000000", "Manual advance must leave CPU intact");
        duration->setValue(3); mode->setCurrentIndex(0); advance->click();
        waitUntil([&] { return advance->isEnabled(); });
        check(clock->text().startsWith("t = 28 µs") && registers->item(0, 1)->text() == "0x00000001", "Timed firmware execution must stop at requested time");
        check(QMetaObject::invokeMethod(assembly, "cellDoubleClicked", Qt::DirectConnection, Q_ARG(int, 0), Q_ARG(int, 1)), "Set GUI breakpoint");
        duration->setValue(100); advance->click(); waitUntil([&] { return advance->isEnabled(); });
        check(clock->text().startsWith("t = 28 µs") && find<QLabel>(window, "sessionState")->text() == "BREAKPOINT", "Timed run must stop before a breakpoint");
        find<QAction>(window, "overviewAction")->trigger(); find<QAction>(window, "virtualTimeViewAction")->trigger();
        duration->setValue(1000000); unit->setCurrentIndex(1); mode->setCurrentIndex(1); advance->click();
        QApplication::processEvents();
        check(find<QPushButton>(window, "pauseVirtualTimeButton")->isEnabled(), "Long virtual advances must remain interruptible");
        find<QPushButton>(window, "pauseVirtualTimeButton")->click();
        const auto pausedClock = clock->text(); QApplication::processEvents();
        check(advance->isEnabled() && clock->text() == pausedClock, "Pause must cancel remaining duration");
        find<QAction>(window, "resetAction")->trigger(); check(clock->text().startsWith("t = 0 µs"), "Reset clears elapsed time");
        if (argc > 1) check(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".time.png"), "Save time controls");
        // Closing/reopening the explorer must not change the main workspace.
        explorer->close(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        check(explorer.isNull() && workspace->currentIndex() == 0, "Explorer close is independent of assembly");
        open->trigger(); explorer = find<QDialog>(window, "peripheralExplorer");
        // Changing even to a Wi-Fi variant of the same chip clears old windows.
        for (const auto board : {picodbg::Board::PicoW, picodbg::Board::Pico2, picodbg::Board::Pico2W, picodbg::Board::Pico}) {
            window.selectBoard(board); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            check(explorer.isNull(), "Board change must close old peripheral explorer");
            check(!find<QAction>(window, "runAction")->isEnabled(), "Board selection must invalidate previous ELF");
            const bool wifi = board == picodbg::Board::PicoW || board == picodbg::Board::Pico2W;
            auto* wiring = find<QTableWidget>(window, "boardWiringTable");
            check(wiring->item(0, 1)->text() == (wifi ? "CYW43 · WL_GPIO0" : "GPIO25"), "Selected board must show its LED wiring");
            window.loadDemo(); open->trigger(); explorer = find<QDialog>(window, "peripheralExplorer");
            if (wifi) {
                choose(find<QComboBox>(*explorer, "peripheralExplorerView"), "@CYW43");
                QApplication::processEvents();
                check(find<QLabel>(*explorer, "boardSummary")->isVisible(), "External radio details must be reachable");
                if (argc > 1 && board == picodbg::Board::Pico2W)
                    check(explorer->grab().save(QString::fromLocal8Bit(argv[1]) + ".pico2w.png"), "Save W board wiring");
            }
        }
        explorer->close(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        find<QAction>(window, "overviewAction")->trigger(); window.resize(1280, 800); QApplication::processEvents();
        check(window.width() <= 1280 && window.height() <= 800, "New views must not enlarge the main window minimum");
        if (argc > 2) {
            window.selectBoard(picodbg::Board::Pico2W); window.loadFile(QString::fromLocal8Bit(argv[2]));
            open->trigger(); explorer = find<QDialog>(window, "peripheralExplorer");
            choose(find<QComboBox>(*explorer, "peripheralExplorerView"), "UART0");
            auto* tabs = find<QTabWidget>(*explorer, "peripheralExplorerTabs"); tabs->setCurrentIndex(1);
            auto* run = find<QAction>(window, "runAction"); run->trigger(); waitUntil([&] { return run->isEnabled(); });
            check(find<QPlainTextEdit>(*tabs->widget(1), "uartOutput")->toPlainText() == "OK", "Independent terminal must receive firmware TX");
            find<QLineEdit>(*tabs->widget(1), "uartInput")->setText("RX");
            find<QPushButton>(*tabs->widget(1), "uartSend")->click();
            check(find<QTableWidget>(*explorer, "peripheralsTable")->item(0, 1)->text() == "0x00000052", "Independent terminal RX injection must update registers without consumption");
            check(explorer->grab().save(QString::fromLocal8Bit(argv[1]) + ".uart-live.png"), "Save live UART");
        }
        std::cout << "GUI session checks passed: independent explorer, detailed views, time controls and four boards\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
