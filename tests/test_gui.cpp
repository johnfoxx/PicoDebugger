#include "main_window.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QKeyEvent>
#include <QPixmap>
#include <QPushButton>
#include <QPointer>
#include <QPlainTextEdit>
#include <QRegion>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T* find(MainWindow& window, const char* name) {
    auto* object = window.findChild<T*>(QString::fromLatin1(name));
    if (!object) throw std::runtime_error(std::string("Missing UI object: ") + name);
    return object;
}
void waitUntil(const std::function<bool()>& condition) {
    QElapsedTimer deadline;
    deadline.start();
    do {
        QApplication::processEvents();
        if (condition()) return;
        QThread::msleep(5);
    } while (deadline.elapsed() < 3000);
    throw std::runtime_error("UI did not reach the expected state within 3 seconds");
}
void toggleBreakpoint(QTableWidget* assembly) {
    // Invoke the same public Qt signal as a user double-click. The test drives
    // actions/widgets, not private emulator state or a duplicate controller.
    require(QMetaObject::invokeMethod(assembly, "cellDoubleClicked", Qt::DirectConnection,
                                     Q_ARG(int, 0), Q_ARG(int, 1)), "Double-click signal unavailable");
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("PicoDebuggerTests");
    QApplication::setApplicationName("PicoDebuggerGuiTests");
    try {
        MainWindow window;
        window.loadDemo();
        window.resize(1800, 1100);
        window.show();
        QApplication::processEvents();
        for (const char* name : {"registers0Table", "registers1Table", "assemblyTable", "memoryTable",
                                 "stackTable", "mappingTable", "peripheralsTable", "watchesTable", "pointsTable"}) {
            auto* view = find<QTableWidget>(window, name);
            require(view->isVisible() && !view->visibleRegion().isEmpty(), "An essential panel is hidden");
            if (view->viewport()->width() < 40 || view->viewport()->height() < 30)
                throw std::runtime_error(std::string("Panel viewport collapsed: ") + name);
        }
        auto* regs = find<QTableWidget>(window, "registers0Table");
        auto* assembly = find<QTableWidget>(window, "assemblyTable");
        auto* memory = find<QTableWidget>(window, "memoryTable");
        auto* step = find<QAction>(window, "stepAction");
        auto* run = find<QAction>(window, "runAction");
        require(regs->item(0, 1)->text() == "0x00000000", "Image must start halted");
        step->trigger(); step->trigger(); step->trigger();
        require(regs->item(0, 1)->text() == "0x00000001", "Step did not refresh R0");
        require(assembly->item(0, 1)->text() == "0x10000206", "Assembly did not follow PC");
        toggleBreakpoint(assembly);
        require(find<QTableWidget>(window, "pointsTable")->rowCount() == 1, "Breakpoint not listed");
        run->trigger();
        waitUntil([run] { return run->isEnabled(); });
        require(assembly->item(0, 1)->text() == "0x10000206", "Breakpoint did not stop before instruction");
        require(memory->item(0, 1)->text().startsWith("00 00 00 00"), "Store executed past breakpoint");
        toggleBreakpoint(assembly);
        run->trigger();
        waitUntil([run] { return run->isEnabled(); });
        require(regs->item(0, 1)->text() == "0x00000005", "Run failed to reach demo BKPT");
        require(memory->item(0, 1)->text().startsWith("05 00 00 00"), "Memory view not refreshed");
        find<QAction>(window, "resetAction")->trigger();
        require(regs->item(0, 1)->text() == "0x00000000", "Reset did not restore register view");
        require(memory->item(0, 1)->text().startsWith("00 00 00 00"), "Reset did not restore memory view");
        for (auto* panel : window.findChildren<QDockWidget*>()) {
            if (panel->isAncestorOf(regs)) panel->hide();
        }
        require(!regs->isVisible(), "Panel hide precondition");
        find<QAction>(window, "overviewAction")->trigger();
        QApplication::processEvents();
        require(regs->isVisible(), "Overview action did not restore CPU panel");
        auto* devices = find<QTreeWidget>(window, "peripheralCatalog");
        auto* peripheralRegisters = find<QTableWidget>(window, "peripheralsTable");
        auto* filter = find<QLineEdit>(window, "peripheralFilter");
        require(devices->topLevelItemCount() >= 35, "Peripheral inventory incomplete");
        QTreeWidgetItem* timer = nullptr;
        QTreeWidgetItem* uart = nullptr;
        for (int i = 0; i < devices->topLevelItemCount(); ++i) {
            auto* item = devices->topLevelItem(i);
            if (item->text(0) == "TIMER") timer = item;
            if (item->text(0) == "UART0") uart = item;
        }
        require(timer && uart, "Timer/UART missing from catalog");
        devices->setCurrentItem(timer);
        require(peripheralRegisters->rowCount() == 17, "Timer register catalog missing");
        require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "Timer live value missing");
        step->trigger();
        require(peripheralRegisters->item(10, 1)->text() == "0x00000001", "Timer view did not update");
        filter->setText("UART0");
        require(timer->isHidden() && !uart->isHidden(), "Peripheral filter mismatch");
        devices->setCurrentItem(uart);
        require(uart->text(1).startsWith("Partiel") && peripheralRegisters->rowCount() == 13, "UART register model missing");
        require(peripheralRegisters->item(2, 1)->text() == "0x00000090", "UART reset flags missing");
        find<QPushButton>(window, "openDeviceWindowButton")->click();
        QPointer<QDialog> uartWindow = find<QDialog>(window, "deviceWindow_UART0");
        require(uartWindow->isVisible() && uartWindow->findChild<QPlainTextEdit*>("uartOutput"), "UART mini window/output missing");
        auto* uartRegisters = uartWindow->findChild<QTableWidget*>("peripheralsTable");
        uartRegisters->setCurrentCell(2, 0); // UARTFR at reset: RXFE=1, TXFE=1.
        auto* uartFields = uartWindow->findChild<QTableWidget*>("registerFieldsTable");
        require(uartFields && uartFields->rowCount() == 5 && uartFields->item(1, 2)->text() == "1", "UART bit values not decoded");
        require(uartFields->item(1, 3)->text().contains("FIFO RX vide : Oui"), "UART bit meaning missing");
        uartWindow->findChild<QLineEdit*>("uartInput")->setText("R");
        uartWindow->findChild<QPushButton*>("uartSend")->click(); // Disabled receiver: report zero accepted bytes.
        const auto i2c = devices->findItems("I2C0", Qt::MatchExactly, 0);
        require(i2c.size() == 1, "I2C inventory missing"); devices->setCurrentItem(i2c.front());
        require(peripheralRegisters->rowCount() >= 30 && i2c.front()->text(1) == "Non émulé", "I2C descriptions must not claim emulation");
        require(peripheralRegisters->item(0, 1)->text() == "Non émulé", "I2C description must not fabricate a reset value");
        auto* regFilter = find<QLineEdit>(window, "registerFilter");
        regFilter->setText("IC_STATUS");
        require(peripheralRegisters->isRowHidden(0), "Register filter did not hide unrelated rows");
        require(peripheralRegisters->item(peripheralRegisters->currentRow(), 0)->text() == "IC_STATUS", "Register filter did not select matching register");
        auto* i2cFields = find<QTableWidget>(window, "registerFieldsTable");
        require(i2cFields->rowCount() == 7 && i2cFields->item(0, 2)->text() == "—", "Unknown register bits must remain unavailable");
        regFilter->setText("no_such_register");
        require(i2cFields->rowCount() == 0, "Empty filter result must clear bit details");
        regFilter->setText("IC_STATUS");
        require(i2cFields->rowCount() == 7, "Restoring a filter must restore bit details");
        regFilter->clear();
        filter->clear(); devices->setCurrentItem(timer);
        find<QPushButton>(window, "openDeviceWindowButton")->click();
        QPointer<QDialog> timerWindow = find<QDialog>(window, "deviceWindow_TIMER");
        require(timerWindow->isVisible() && uartWindow->isVisible(), "Multiple peripheral windows must coexist");
        auto* timerValues = timerWindow->findChild<QTableWidget*>("peripheralsTable");
        step->trigger(); require(timerValues->item(10, 1)->text() == "0x00000002", "Mini window values not refreshed");
        timerWindow->close(); QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(timerWindow.isNull() && uartWindow, "Closing a mini window affects only that window");
        find<QAction>(window, "peripheralWindowAction")->trigger();
        QApplication::processEvents();
        bool floating = false;
        for (auto* panel : window.findChildren<QDockWidget*>()) if (panel->isAncestorOf(devices)) floating = panel->isFloating();
        require(floating, "Dedicated peripheral window did not open");
        if (argc == 2) {
            for (auto* panel : window.findChildren<QDockWidget*>())
                if (panel->isAncestorOf(devices)) require(panel->grab().save(QString::fromLocal8Bit(argv[1]) + ".peripherals.png"), "Cannot save peripheral screenshot");
        }
        find<QAction>(window, "overviewAction")->trigger(); QApplication::processEvents();
        if (argc == 2) require(window.grab().save(QString::fromLocal8Bit(argv[1])), "Cannot save GUI screenshot");
        // Profiles have different register maps. Rebuild the catalog and clear
        // any values belonging to the previous session rather than reusing them.
        // Reproduce selection through the popup: setCurrentIndex alone never
        // exercised the Qt5/Wayland activation regression reported by users.
        auto* chipSelector = find<QComboBox>(window, "chipSelector");
        chipSelector->showPopup();
        auto* choiceView = chipSelector->view();
        require(choiceView->isVisible(), "Chip popup not visible");
        QKeyEvent nextChip(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QKeyEvent chooseChip(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(choiceView, &nextChip);
        QApplication::sendEvent(choiceView, &chooseChip);
        require(chipSelector->currentIndex() == 1 && !choiceView->isVisible(), "Chip selection through popup failed");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(uartWindow.isNull(), "Profile change must close windows from the old target");
        require(devices->topLevelItemCount() >= 60, "RP2350 catalog not rebuilt");
        const auto timer1 = devices->findItems("TIMER1", Qt::MatchExactly, 0);
        require(timer1.size() == 1, "RP2350 TIMER1 missing");
        devices->setCurrentItem(timer1.front());
        require(peripheralRegisters->rowCount() == 19, "RP2350 timer offsets not described");
        require(peripheralRegisters->item(10, 1)->text() == "Pas d'image", "Old session value survived profile change");
        window.loadDemo();
        require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "RP2350 timer reset value");
        step->trigger(); require(peripheralRegisters->item(10, 1)->text() == "0x00000001", "RP2350 TIMER1 must advance");
        find<QPushButton>(window, "openDeviceWindowButton")->click();
        auto* timer1Window = find<QDialog>(window, "deviceWindow_TIMER1");
        find<QComboBox>(window, "coreSelector")->setCurrentIndex(1);
        const auto sio = devices->findItems("SIO", Qt::MatchExactly, 0);
        require(sio.size() == 1, "SIO missing");
        devices->setCurrentItem(sio.front());
        require(peripheralRegisters->item(0, 1)->text() == "0x00000001", "SIO inspection did not select core1 context");
        find<QPushButton>(window, "openDeviceWindowButton")->click();
        auto* sioWindow = find<QDialog>(window, "deviceWindow_SIO");
        auto* sioContext = sioWindow->findChild<QComboBox*>("inspectionCore");
        sioContext->setCurrentIndex(0);
        require(sioWindow->findChild<QTableWidget*>("peripheralsTable")->item(0, 1)->text() == "0x00000000", "Mini window has independent core context");
        require(peripheralRegisters->item(0, 1)->text() == "0x00000001", "Mini window must not change main context");
        if (argc == 2) {
            require(timer1Window->grab().save(QString::fromLocal8Bit(argv[1]) + ".timer1.png"), "Cannot save timer1 mini window");
            require(sioWindow->grab().save(QString::fromLocal8Bit(argv[1]) + ".sio.png"), "Cannot save SIO mini window");
        }
        if (argc == 3) {
            window.loadFile(QString::fromLocal8Bit(argv[2]));
            devices->setCurrentItem(devices->findItems("UART0", Qt::MatchExactly, 0).front());
            find<QPushButton>(window, "openDeviceWindowButton")->click();
            auto* liveUart = find<QDialog>(window, "deviceWindow_UART0");
            run->trigger(); waitUntil([run] { return run->isEnabled(); });
            require(liveUart->findChild<QPlainTextEdit*>("uartOutput")->toPlainText() == "OK", "ELF UART output must reach the mini window");
            liveUart->findChild<QLineEdit*>("uartInput")->setText("RX");
            liveUart->findChild<QPushButton*>("uartSend")->click();
            require(liveUart->findChild<QTableWidget*>("peripheralsTable")->item(0, 1)->text() == "0x00000052", "Injected UART RX must appear without inspection consuming it");
            QApplication::processEvents();
            require(liveUart->grab().save(QString::fromLocal8Bit(argv[1]) + ".uart.png"), "Cannot save UART window");
            require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".rp2350.png"), "Cannot save live Pico2 view");
        }
        std::cout << "GUI checks passed: overview, execution, profiles, live devices, mini windows, contexts, lifecycle\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI FAIL: " << error.what() << '\n';
        return 1;
    }
}
