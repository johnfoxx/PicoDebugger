#include "main_window.hpp"
#include "inspection_workspace.hpp"
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
#include <QTabWidget>
#include <QHeaderView>
#include <QLabel>
#include <QStyleFactory>
#include <QTreeWidget>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T* find(QObject& window, const char* name) {
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
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setOrganizationName("PicoDebuggerTests");
    QApplication::setApplicationName("PicoDebuggerGuiTests");
    try {
        MainWindow window;
        window.resize(1366, 900);
        window.show();
        QApplication::processEvents();
        require(!find<QAction>(window, "stepAction")->isEnabled(), "Empty session must not execute");
        require(find<QPushButton>(window, "welcomeDemoButton")->isVisible(), "Empty session needs a clear entry action");
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".welcome.png"), "Cannot save welcome page");
        find<QPushButton>(window, "welcomeDemoButton")->click();
        QApplication::processEvents();
        auto* workspace = find<QTabWidget>(window, "inspectionWorkspace");
        require(workspace->count() == 2, "Start with Code and Peripherals, no floating inspectors");
        require(window.findChildren<QDockWidget*>().size() == 3, "Too many competing docks in the default layout");
        for (const char* name : {"registersTable", "assemblyTable", "memoryTable"}) {
            auto* view = find<QTableWidget>(window, name);
            require(view->isVisible() && !view->visibleRegion().isEmpty(), "An essential panel is hidden");
            require(view->viewport()->width() >= 200 && view->viewport()->height() >= 60, "Essential viewport collapsed");
        }
        auto* regs = find<QTableWidget>(window, "registersTable");
        require(regs->columnCount() == 3 && regs->item(0, 2)->text() == "0x00000000", "Both CPU values must be visible");
        for (const auto& view : {std::make_pair("stackViewAction", "stackTable"),
                                std::make_pair("mappingViewAction", "mappingTable"),
                                std::make_pair("consoleViewAction", "consoleView"),
                                std::make_pair("pointsViewAction", "pointsTable")}) {
            find<QAction>(window, view.first)->trigger(); QApplication::processEvents();
            require(find<QWidget>(window, view.second)->isVisible(), "Secondary view not reachable from menu");
        }
        find<QAction>(window, "overviewAction")->trigger(); QApplication::processEvents();
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".overview.png"), "Cannot save overview");
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
        find<QAction>(window, "peripheralWindowAction")->trigger(); QApplication::processEvents();
        auto* catalogPage = find<QWidget>(window, "peripheralCatalogPage");
        auto* devices = find<QTreeWidget>(*catalogPage, "peripheralCatalog");
        auto* peripheralRegisters = find<QTableWidget>(*catalogPage, "peripheralsTable");
        auto* filter = find<QLineEdit>(*catalogPage, "peripheralFilter");
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
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        QPointer<QWidget> uartWindow = find<QWidget>(window, "deviceInspection_UART0");
        require(workspace->indexOf(uartWindow) >= 2 && window.findChildren<QDialog*>().isEmpty(), "Inspect in a tab before opening windows");
        const int openedTabs = workspace->count();
        find<QAction>(window, "peripheralWindowAction")->trigger();
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        require(workspace->count() == openedTabs && workspace->currentWidget() == uartWindow, "Opening an existing peripheral must reuse its inspector");
        auto* uartFilter = find<QLineEdit>(*uartWindow, "registerFilter");
        uartFilter->setText("UARTFR");
        find<QLineEdit>(*uartWindow, "uartInput")->setText("unsent text");
        find<QPushButton>(*uartWindow, "detachInspectionButton")->click();
        QPointer<QDialog> detachedUart = find<QDialog>(window, "deviceWindow_UART0");
        require(detachedUart->isVisible() && workspace->indexOf(uartWindow) == -1, "Detach must move the same inspector");
        find<QAction>(window, "gatherInspectorsAction")->trigger();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(detachedUart.isNull() && workspace->currentWidget() == uartWindow, "Gather must reattach and remove floating shells");
        require(uartFilter->text() == "UARTFR" && find<QLineEdit>(*uartWindow, "uartInput")->text() == "unsent text", "Detaching lost filter or unsent UART input");
        uartFilter->clear();
        require(uartWindow->isVisible() && uartWindow->findChild<QPlainTextEdit*>("uartOutput"), "UART mini window/output missing");
        auto* uartRegisters = uartWindow->findChild<QTableWidget*>("peripheralsTable");
        uartRegisters->setCurrentCell(2, 0); // UARTFR at reset: RXFE=1, TXFE=1.
        auto* uartFields = uartWindow->findChild<QTableWidget*>("registerFieldsTable");
        require(uartFields && uartFields->rowCount() == 5 && uartFields->item(1, 2)->text() == "1", "UART bit values not decoded");
        require(uartFields->item(1, 3)->text().contains("FIFO RX vide : Oui"), "UART bit meaning missing");
        find<QPushButton>(*uartWindow, "registerDetailsButton")->click();
        require(uartFields->isVisible() && !uartRegisters->isVisible(), "Bits must replace the values view without stacking another panel");
        find<QPushButton>(*uartWindow, "registerDetailsButton")->click();
        find<QTabWidget>(*uartWindow, "uartTabs")->setCurrentIndex(1);
        require(find<QLineEdit>(*uartWindow, "uartInput")->isVisible(), "UART terminal must be reachable");
        uartWindow->findChild<QLineEdit*>("uartInput")->setText("R");
        uartWindow->findChild<QPushButton*>("uartSend")->click(); // Disabled receiver: report zero accepted bytes.
        find<QAction>(window, "peripheralWindowAction")->trigger();
        const auto i2c = devices->findItems("I2C0", Qt::MatchExactly, 0);
        require(i2c.size() == 1, "I2C inventory missing"); devices->setCurrentItem(i2c.front());
        require(peripheralRegisters->rowCount() >= 30 && i2c.front()->text(1) == "Non émulé", "I2C descriptions must not claim emulation");
        require(peripheralRegisters->item(0, 1)->text() == "Non émulé", "I2C description must not fabricate a reset value");
        auto* regFilter = find<QLineEdit>(*catalogPage, "registerFilter");
        regFilter->setText("IC_STATUS");
        require(peripheralRegisters->isRowHidden(0), "Register filter did not hide unrelated rows");
        require(peripheralRegisters->item(peripheralRegisters->currentRow(), 0)->text() == "IC_STATUS", "Register filter did not select matching register");
        auto* i2cFields = find<QTableWidget>(*catalogPage, "registerFieldsTable");
        require(i2cFields->rowCount() == 7 && i2cFields->item(0, 2)->text() == "—", "Unknown register bits must remain unavailable");
        regFilter->setText("no_such_register");
        require(i2cFields->rowCount() == 0, "Empty filter result must clear bit details");
        regFilter->setText("IC_STATUS");
        require(i2cFields->rowCount() == 7, "Restoring a filter must restore bit details");
        regFilter->clear();
        filter->clear(); devices->setCurrentItem(timer);
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        QPointer<QWidget> timerWindow = find<QWidget>(window, "deviceInspection_TIMER");
        require(timerWindow->isVisible() && uartWindow && workspace->indexOf(uartWindow) >= 2, "Several inspection tabs must coexist");
        auto* timerValues = timerWindow->findChild<QTableWidget*>("peripheralsTable");
        step->trigger(); require(timerValues->item(10, 1)->text() == "0x00000002", "Mini window values not refreshed");
        require(QMetaObject::invokeMethod(workspace, "tabCloseRequested", Qt::DirectConnection,
                                         Q_ARG(int, workspace->indexOf(timerWindow))), "Close-tab signal unavailable");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(timerWindow.isNull() && uartWindow, "Closing a mini window affects only that window");
        find<QAction>(window, "peripheralWindowAction")->trigger();
        QApplication::processEvents();
        require(workspace->currentWidget() == catalogPage && catalogPage->isVisible(), "Ctrl+P must reveal the integrated catalogue");
        filter->setText("no_such_peripheral");
        require(find<QLabel>(*catalogPage, "peripheralEmptyState")->isVisible(), "Empty catalogue search needs feedback");
        require(!find<QPushButton>(*catalogPage, "openDeviceWindowButton")->isEnabled(), "Empty search must not open a hidden device");
        filter->clear(); devices->setCurrentItem(timer);
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".peripherals.png"), "Cannot save catalogue");
        find<QAction>(window, "overviewAction")->trigger(); QApplication::processEvents();
        window.resize(1280, 800); QApplication::processEvents();
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".compact.png"), "Cannot save compact layout");
        if (window.width() > 1280 || window.height() > 800)
            throw std::runtime_error("Layout exceeds a small desktop: " + std::to_string(window.width()) + "x" + std::to_string(window.height())
                + " workspace minimum " + std::to_string(workspace->minimumSizeHint().width()) + "x" + std::to_string(workspace->minimumSizeHint().height()));
        require(regs->viewport()->width() >= regs->columnWidth(0) + regs->columnWidth(1) + regs->columnWidth(2), "CPU values clipped on small desktop");
        require(assembly->viewport()->width() >= 380, "Code area too narrow on small desktop");
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".compact.png"), "Cannot save compact layout");
        window.resize(1366, 900); QApplication::processEvents();
        // Exercise profile cleanup while an inspector is detached.
        find<QPushButton>(*uartWindow, "detachInspectionButton")->click();
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
        require(uartWindow.isNull() && workspace->count() == 2 && window.findChildren<QDialog*>().isEmpty(), "Profile change must remove all old inspectors");
        find<QAction>(window, "peripheralWindowAction")->trigger();
        require(devices->topLevelItemCount() >= 60, "RP2350 catalog not rebuilt");
        const auto timer1 = devices->findItems("TIMER1", Qt::MatchExactly, 0);
        require(timer1.size() == 1, "RP2350 TIMER1 missing");
        devices->setCurrentItem(timer1.front());
        require(peripheralRegisters->rowCount() == 19, "RP2350 timer offsets not described");
        require(peripheralRegisters->item(10, 1)->text() == "Pas d'image", "Old session value survived profile change");
        window.loadDemo();
        require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "RP2350 timer reset value");
        step->trigger(); require(peripheralRegisters->item(10, 1)->text() == "0x00000001", "RP2350 TIMER1 must advance");
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        auto* timer1Window = find<QWidget>(window, "deviceInspection_TIMER1");
        find<QComboBox>(window, "coreSelector")->setCurrentIndex(1);
        const auto sio = devices->findItems("SIO", Qt::MatchExactly, 0);
        require(sio.size() == 1, "SIO missing");
        devices->setCurrentItem(sio.front());
        require(peripheralRegisters->item(0, 1)->text() == "0x00000001", "SIO inspection did not select core1 context");
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        auto* sioWindow = find<QWidget>(window, "deviceInspection_SIO");
        auto* sioContext = sioWindow->findChild<QComboBox*>("inspectionCore");
        sioContext->setCurrentIndex(0);
        require(sioWindow->findChild<QTableWidget*>("peripheralsTable")->item(0, 1)->text() == "0x00000000", "Mini window has independent core context");
        require(peripheralRegisters->item(0, 1)->text() == "0x00000001", "Mini window must not change main context");
        const int beforeDuplicate = workspace->count();
        find<QAction>(*sioWindow, "duplicateInspectionAction")->trigger();
        QPointer<QWidget> duplicate = workspace->currentWidget();
        require(workspace->count() == beforeDuplicate + 1 && duplicate != sioWindow, "Explicit duplicate inspection missing");
        find<QComboBox>(*duplicate, "inspectionCore")->setCurrentIndex(1);
        require(find<QTableWidget>(*duplicate, "peripheralsTable")->item(0, 1)->text() == "0x00000001", "Duplicate must have its own core context");
        require(find<QTableWidget>(*sioWindow, "peripheralsTable")->item(0, 1)->text() == "0x00000000", "Duplicate changed original context");
        require(QMetaObject::invokeMethod(workspace, "tabCloseRequested", Qt::DirectConnection,
                                         Q_ARG(int, workspace->indexOf(duplicate))), "Cannot close duplicate");
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        require(duplicate.isNull() && workspace->indexOf(sioWindow) >= 2, "Closing duplicate lost the original");
        if (argc == 2) {
            require(timer1Window->grab().save(QString::fromLocal8Bit(argv[1]) + ".timer1.png"), "Cannot save timer1 mini window");
            require(sioWindow->grab().save(QString::fromLocal8Bit(argv[1]) + ".sio.png"), "Cannot save SIO mini window");
        }
        if (argc == 3) {
            window.loadFile(QString::fromLocal8Bit(argv[2]));
            devices->setCurrentItem(devices->findItems("UART0", Qt::MatchExactly, 0).front());
            find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
            auto* liveUart = find<QWidget>(window, "deviceInspection_UART0");
            find<QTabWidget>(*liveUart, "uartTabs")->setCurrentIndex(1);
            run->trigger(); waitUntil([run] { return run->isEnabled(); });
            require(liveUart->findChild<QPlainTextEdit*>("uartOutput")->toPlainText() == "OK", "ELF UART output must reach the mini window");
            liveUart->findChild<QLineEdit*>("uartInput")->setText("RX");
            liveUart->findChild<QPushButton*>("uartSend")->click();
            require(liveUart->findChild<QTableWidget*>("peripheralsTable")->item(0, 1)->text() == "0x00000052", "Injected UART RX must appear without inspection consuming it");
            QApplication::processEvents();
            require(liveUart->grab().save(QString::fromLocal8Bit(argv[1]) + ".uart.png"), "Cannot save UART window");
            require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".uart-workspace.png"), "Cannot save UART workspace");
            find<QComboBox>(window, "coreSelector")->setCurrentIndex(0);
            find<QAction>(window, "codeViewAction")->trigger();
            require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".rp2350.png"), "Cannot save live Pico2 view");
        }
        std::cout << "GUI checks passed: compact layout, navigation, execution, devices, tab reuse, detach/gather, contexts and profile cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI FAIL: " << error.what() << '\n';
        return 1;
    }
}
