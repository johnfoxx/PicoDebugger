#include "main_window.hpp"
#include "memory_table.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QSettings>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
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
void submitDialog(QAction* opener, const std::function<void(QDialog&, QPushButton*)>& fill) {
    std::exception_ptr error;
    bool opened = false;
    QTimer submit;
    submit.setSingleShot(true);
    QObject::connect(&submit, &QTimer::timeout, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        try {
            check(dialog, "Expected an edit/watch dialog");
            opened = true;
            auto* buttons = dialog->findChild<QDialogButtonBox*>();
            check(buttons, "Missing dialog submit button");
            auto* accept = buttons->button(QDialogButtonBox::Ok);
            fill(*dialog, accept);
            accept->click();
            check(!dialog->isVisible(), "Valid input did not close the dialog");
        } catch (...) {
            error = std::current_exception();
            if (dialog) dialog->reject();
        }
    });
    submit.start(0);
    opener->trigger();
    submit.stop();
    if (error) std::rethrow_exception(error);
    check(opened, "Click did not open a dialog");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setOrganizationName("PicoDebuggerTests"); QApplication::setApplicationName("PicoDebuggerSessionTests");
    try {
        QTemporaryDir settings;
        check(settings.isValid(), "Temporary settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
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
        check(find<QComboBox>(window, "coreSelector")->currentIndex() == 2, "Explorer must preserve main CPU selection");
        check(!find<QTreeWidget>(*explorer, "peripheralCatalog")->isVisible(), "Focused view must dedicate space to registers");
        auto* dark = find<QAction>(window, "picoThemeDark");
        auto* light = find<QAction>(window, "picoThemeLight");
        const bool wasLight = light->isChecked();
        dark->trigger();
        auto* readOnly = values->item(0, 3);
        auto* writable = values->item(1, 3);
        const auto darkReadOnly = readOnly->foreground().color();
        const auto darkWritable = writable->foreground().color();
        check(readOnly->text() == "RO" && writable->text() == "RW" && darkReadOnly != darkWritable &&
              !readOnly->icon().isNull() && !writable->icon().isNull(), "Register access colours and labels");
        auto* catalogue = find<QTreeWidget>(*explorer, "peripheralCatalog");
        const auto* sio = catalogue->findItems("SIO", Qt::MatchExactly).at(0);
        const auto mixed = sio->foreground(0).color();
        check(sio->toolTip(0).contains("Accès mixte") && mixed != darkReadOnly && mixed != darkWritable, "Mixed peripheral access");
        auto* map = find<QTableWidget>(window, "mappingTable");
        QTableWidgetItem* flash = nullptr;
        QTableWidgetItem* ram = nullptr;
        for (int i = 0; i < map->rowCount(); ++i) {
            if (map->item(i, 0)->text() == "0x10000000") flash = map->item(i, 3);
            if (map->item(i, 0)->text() == "0x20000000") ram = map->item(i, 3);
        }
        check(flash && ram && flash->foreground().color() == darkReadOnly && ram->foreground().color() == darkWritable,
              "Memory map shares the register access colours");
        auto* memoryAddress = find<QLineEdit>(window, "memoryAddress");
        auto* memory = find<MemoryTable>(window, "memoryTable");
        auto readAt = [&](const QString& address) {
            memoryAddress->setText(address);
            QMetaObject::invokeMethod(memoryAddress, "returnPressed", Qt::DirectConnection);
            return memory->item(0, 0)->foreground().color();
        };
        check(readAt("0x10000000") == darkReadOnly && readAt("0x101ffffe") == mixed &&
              readAt("0x20000000") == darkWritable, "Memory rows show read-only, mixed or writable access");
        find<QPushButton>(window, "nextMemoryPage")->click();
        check(memory->item(0, 0)->text() == "0x20000100", "Next memory page advances by the display length");
        find<QPushButton>(window, "previousMemoryPage")->click();
        check(memory->item(0, 0)->text() == "0x20000000", "Previous memory page returns to the preceding address");
        light->trigger();
        check(readOnly->foreground().color() != darkReadOnly && writable->foreground().color() != darkWritable &&
              flash->foreground().color() == readOnly->foreground().color() &&
              memory->item(0, 0)->foreground().color() == writable->foreground().color(),
              "Access colours update consistently in the light theme");
        if (!wasLight) dark->trigger();
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
        auto* watchTree = find<QTreeWidget>(window, "valueWatchesTree");
        find<QPushButton>(*explorer, "watchPeripheralButton")->click();
        auto* timerWatch = watchTree->topLevelItem(0);
        check(timerWatch && timerWatch->text(0) == "TIMER" && timerWatch->childCount() == 17, "Follow an entire peripheral");
        timerWatch->setExpanded(true);
        find<QAction>(window, "stepAction")->trigger();
        check(timerWatch->child(10)->text(1) == "0x00000001", "Peripheral watch follows steps");
        timerWatch->setExpanded(false);
        choose(view, "SIO");
        auto* writeRegister = find<QAction>(*explorer, "writePeripheralRegisterAction");
        check(!writeRegister->isEnabled(), "CPUID is read-only");
        for (int i = 0; i < values->rowCount(); ++i) {
            if (values->item(i, 0)->text() == "SIO.GPIO_OUT") values->setCurrentCell(i, 0);
        }
        check(writeRegister->isEnabled(), "GPIO output is writable");
        auto* watchRegister = find<QAction>(*explorer, "watchRegisterAction");
        watchRegister->trigger();
        auto* gpioWatch = watchTree->topLevelItem(1);
        check(gpioWatch && gpioWatch->text(0) == "SIO.GPIO_OUT" && gpioWatch->text(2) == "1", "Register watch preserves core");
        submitDialog(writeRegister, [](QDialog& dialog, QPushButton*) {
            find<QLineEdit>(dialog, "registerWriteValue")->setText("0x1234");
        });
        check(gpioWatch->text(1) == "0x00001234" && values->item(values->currentRow(), 1)->text() == "0x00001234",
              "Register edit refreshes the table and watch");
        choose(view, "UART0");
        find<QPushButton>(*explorer, "watchPeripheralButton")->click();
        check(watchTree->topLevelItemCount() == 3 && watchTree->topLevelItem(2)->text(0) == "UART0", "Multiple peripherals and registers coexist");
        find<QAction>(window, "memoryViewAction")->trigger();
        memory->selectAddress(0x20000001);
        submitDialog(find<QAction>(window, "watchMemoryAction"), [](QDialog& dialog, QPushButton*) {
            check(find<QLineEdit>(dialog, "memoryValueAddress")->text() == "0x20000001", "Watch uses the selected byte, not the row address");
        });
        auto* memoryWatch = watchTree->topLevelItem(3);
        check(memoryWatch && memoryWatch->text(1) == "0x00", "Watch an address at the chosen width");
        submitDialog(find<QAction>(window, "writeMemoryAction"), [](QDialog& dialog, QPushButton* write) {
            check(find<QLineEdit>(dialog, "memoryValueAddress")->text() == "0x20000001", "Edit uses the selected byte");
            auto* value = find<QLineEdit>(dialog, "memoryWriteValue");
            value->setText("0x100"); write->click();
            check(dialog.isVisible(), "An overflowing write should keep the editor open");
            value->setText("0x41");
        });
        check(memoryWatch->text(1) == "0x41", "Memory edit refreshes its watch");
        check(memory->item(0, 1)->text() == "00" && memory->item(0, 2)->text() == "41" &&
              memory->item(0, 17)->text().startsWith(".A"), "Memory edit refreshes hex and ASCII together");
        check(memoryWatch->background(0).color() == memory->item(0, 2)->background().color() &&
              memory->item(0, 1)->background().style() == Qt::NoBrush, "Watch colour marks the exact byte");
        memory->selectAddress(0x20000010);
        submitDialog(find<QAction>(window, "watchMemoryAction"), [](QDialog& dialog, QPushButton*) {
            auto* width = find<QComboBox>(dialog, "memoryValueWidth");
            width->setCurrentIndex(width->findData(0));
            find<QSpinBox>(dialog, "memoryWatchLength")->setValue(12);
        });
        auto* range = watchTree->topLevelItem(4);
        check(range && range->childCount() == 3 && range->child(0)->text(1) == "00 00 00 00", "A watched region previews its bytes");
        memory->selectAddress(0x2000001b);
        check(memory->currentItem()->background().color() == range->background(0).color(), "End of range uses the watch colour");
        memory->selectAddress(0x2000001c);
        check(memory->currentItem()->background().style() == Qt::NoBrush, "Range colour stops at the watched boundary");
        find<QPushButton>(window, "openWatchMemoryButton")->click();
        QApplication::processEvents();
        check(memory->isVisible() && workspace->isVisible() &&
              memory->item(0, 0)->text() == "0x20000010" && watchTree->isVisible(), "A watch opens in the independent memory panel");
        check(memory->visualItemRect(memory->item(0, 17)).right() < memory->viewport()->width() &&
              registers->isVisible(), "The memory grid fits ASCII while the CPU comparison stays visible");
        find<QPushButton>(window, "removeValueWatchButton")->click();
        check(memory->item(0, 1)->background().style() == Qt::NoBrush, "Removing a watch removes its memory colour");
        watchTree->setCurrentItem(timerWatch);
        find<QPushButton>(window, "openWatchMemoryButton")->click();
        auto* memoryContext = find<QLabel>(window, "memoryContext");
        check(memoryContext->text().contains("Périphérique TIMER") && memoryContext->text().contains("+0x0") &&
              memory->item(0, 1)->background().color() == timerWatch->background(0).color(), "Peripheral watch opens its coloured register space");
        choose(view, "SIO");
        find<QPushButton>(*explorer, "openPeripheralMemoryButton")->click();
        check(memoryContext->text().contains("Périphérique SIO") && memoryContext->text().contains("SIO.CPUID"),
              "The memory view identifies an unwatched peripheral and register");
        QApplication::processEvents();
        if (argc > 1) check(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".memory.png"), "Save main memory and watch views");
        watchTree->setCurrentItem(gpioWatch);
        find<QPushButton>(window, "openValueWatchButton")->click();
        check(memory->isVisible() && registers->isVisible(), "Opening a peripheral watch keeps Memory and CPU visible");
        auto* zoom = find<QWidget>(window, "deviceInspection_SIO");
        auto* zoomRegisters = find<QTableWidget>(*zoom, "peripheralsTable");
        check(workspace->currentWidget() == zoom && find<QComboBox>(*zoom, "inspectionCore")->currentIndex() == 1
              && zoomRegisters->item(zoomRegisters->currentRow(), 0)->text() == "SIO.GPIO_OUT", "Opening a watch zooms to the corresponding register and core");
        if (argc > 1) check(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".watches.png"), "Save watch and zoom views");
        check(find<QAction>(*zoom, "watchRegisterAction")->isChecked(), "Follow state is shared with the zoom view");
        find<QPushButton>(window, "removeValueWatchButton")->click();
        check(watchTree->topLevelItemCount() == 3 && !find<QAction>(*zoom, "watchRegisterAction")->isChecked(),
              "Removing a watch updates the selection controls");
        find<QAction>(window, "resetAction")->trigger();
        check(watchTree->topLevelItemCount() == 3 && memoryWatch->text(1) == "0x00", "Reset keeps the watch and refreshes its value");
        find<QAction>(window, "overviewAction")->trigger();
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
            const bool pico2 = board == picodbg::Board::Pico2 || board == picodbg::Board::Pico2W;
            check(watchTree->topLevelItemCount() == (pico2 ? 0 : 3), "Watch addresses stay associated with the chip");
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
