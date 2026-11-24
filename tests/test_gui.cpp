#include "main_window.hpp"
#include "inspection_workspace.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QKeyEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QPointer>
#include <QPlainTextEdit>
#include <QRegion>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTabWidget>
#include <QHeaderView>
#include <QLabel>
#include <QStyleFactory>
#include <QTreeWidget>
#include <QToolButton>
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
        QTemporaryDir settings;
        require(settings.isValid(), "Temporary settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        MainWindow window;
        window.resize(1366, 900);
        window.show();
        QApplication::processEvents();
        require(!find<QAction>(window, "stepAction")->isEnabled(), "Empty session must not execute");
        require(find<QAction>(window, "demoAction")->isEnabled(), "Empty session needs a clear entry action");
        if (argc >= 2) require(window.grab().save(QString::fromLocal8Bit(argv[1]) + ".welcome.png"), "Cannot save welcome page");
        find<QAction>(window, "demoAction")->trigger();
        QApplication::processEvents();
        auto* workspace = find<QTabWidget>(window, "inspectionWorkspace");
        require(workspace->count() == 1 && workspace->isVisible(), "Start with the peripheral catalogue");
        auto* memoryDock = find<QDockWidget>(window, "memoryDock");
        auto* ioDock = find<QDockWidget>(window, "peripheralDock");
        auto* cpuDock = find<QDockWidget>(window, "cpuDock");
        auto* stackDock = find<QDockWidget>(window, "stackDock");
        auto* assemblyDock = find<QDockWidget>(window, "assemblyDock");
        auto* monitorDock = find<QDockWidget>(window, "monitorDock");
        require(memoryDock->isVisible() && ioDock->isVisible() && monitorDock->isVisible(), "Memory, peripherals and watches coexist");
        require(monitorDock->width() <= 155, "Surveillance leaves width for memory and stack details");
        require(cpuDock->x() < memoryDock->x() && memoryDock->x() < monitorDock->x() &&
                memoryDock->y() < ioDock->y() && cpuDock->y() < stackDock->y() && ioDock->y() < assemblyDock->y() &&
                assemblyDock->y() < find<QDockWidget>(window, "toolsDock")->y(),
                "Default arrangement puts stack below the CPU and assembler and tools below the middle panels");
        for (const char* name : {"registersTable", "assemblyTable", "memoryTable"}) {
            auto* view = find<QTableWidget>(window, name);
            require(view->isVisible() && !view->visibleRegion().isEmpty(), "An essential panel is hidden");
            const int minHeight = view->rowHeight(0) * 2;
            if (view->viewport()->width() < 200 || view->viewport()->height() < minHeight)
                throw std::runtime_error(std::string("Essential viewport collapsed: ") + name + " "
                    + std::to_string(view->viewport()->width()) + "x" + std::to_string(view->viewport()->height()));
        }
        require(cpuDock->widget()->minimumHeight() == cpuDock->widget()->maximumHeight() &&
                stackDock->geometry().bottom() == monitorDock->geometry().bottom(), "Fixed CPU height leaves the full remaining column to the stack");
        auto* regs = find<QTableWidget>(window, "registersTable");
        require(regs->columnCount() == 4 && !regs->isColumnHidden(1) && regs->item(0, 1)->text() == "0x00000000",
                "Core 0 registers are visible at startup");
        require(regs->visualItemRect(regs->item(20, 1)).bottom() < regs->viewport()->height(), "All standard register rows fit without scrolling");
        auto* stack0 = find<QTreeWidget>(window, "stackCore0");
        auto* stack1 = find<QTreeWidget>(window, "stackCore1");
        auto* coreSelector = find<QComboBox>(window, "coreSelector");
        require(coreSelector->currentIndex() == 0 && stack0->isVisible() && !stack1->isVisible() &&
                stack0->topLevelItem(0)->text(0).startsWith("#0 "), "Core 0 and its current stack frame are shown at startup");
        require(!regs->isColumnHidden(3) && regs->isColumnHidden(2) && !regs->item(15, 3)->text().isEmpty() &&
                stack0->isVisible() && !stack1->isVisible() && !stack0->isColumnHidden(3), "Single core keeps stack ASCII and adds register meanings");
        coreSelector->setCurrentIndex(1);
        require(regs->isColumnHidden(1) && !regs->isColumnHidden(2) && !stack0->isVisible() && stack1->isVisible(), "Core 1 fills the focus area");
        coreSelector->setCurrentIndex(0);
        coreSelector->setCurrentIndex(2);
        require(!regs->isColumnHidden(1) && !regs->isColumnHidden(2) && regs->isColumnHidden(3) &&
                regs->item(0, 2)->text() == "0x00000000" && stack0->isVisible() && stack1->isVisible(), "All restores both contexts");
        find<QAction>(window, "focusView_memory")->trigger();
        require(memoryDock->isVisible() && ioDock->isHidden() && assemblyDock->isHidden(), "Memory focus");
        find<QAction>(window, "focusView_split")->trigger();
        require(memoryDock->isVisible() && ioDock->isVisible() && assemblyDock->isHidden(), "Memory and peripherals split");
        find<QAction>(window, "focusView_all")->trigger();
        require(assemblyDock->isVisible(), "Restore standalone views");
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
        find<QAction>(window, "userManualAction")->trigger();
        auto* manual = find<QDialog>(window, "userManualDialog");
        auto* manualText = manual->findChild<QTextBrowser*>();
        require(manualText && manualText->toPlainText().contains("Organiser les panneaux"), "The bundled user manual opens in Help");
        manual->close();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* step = find<QAction>(window, "stepAction");
        auto* run = find<QAction>(window, "runAction");
        const auto console = [&](const QString& command) {
            auto* input = find<QLineEdit>(window, "consoleExpression");
            input->setText(command);
            QMetaObject::invokeMethod(input, "returnPressed");
        };
        auto* consoleToggle = find<QAction>(window, "consoleToggleAction");
        const auto shownHeight = memoryDock->height() + ioDock->height() + assemblyDock->height();
        require(consoleToggle->isChecked(), "Console open by default");
        consoleToggle->trigger();
        QApplication::processEvents();
        require(find<QDockWidget>(window, "toolsDock")->isHidden() &&
                memoryDock->height() + ioDock->height() + assemblyDock->height() > shownHeight, "Folding console frees central space");
        consoleToggle->trigger();
        require(find<QPlainTextEdit>(window, "consoleView")->isVisible(), "Toolbar restores the console");
        auto* toolsToggle = find<QToolButton>(window, "toolsToggleButton");
        toolsToggle->click();
        require(find<QDockWidget>(window, "toolsDock")->isHidden(), "Small bottom button folds tools");
        toolsToggle->click();
        require(find<QPlainTextEdit>(window, "consoleView")->isVisible(), "Small bottom button restores the active tool");
        console("bt all");
        const auto journal = find<QPlainTextEdit>(window, "consoleView")->toPlainText();
        require(journal.contains("Backtrace · cœur 0") && journal.contains("#0 0x10000200") &&
                journal.contains("Backtrace · cœur 1"), "Console can address both cores");
        console("watch 0x20000000, 4");
        console("break *0x10000206");
        require(find<QTableWidget>(window, "pointsTable")->rowCount() == 2, "Console creates execution and data breakpoints");
        console("delete breakpoints");
        require(find<QTableWidget>(window, "pointsTable")->rowCount() == 1, "Removing all execution breakpoints preserves watchpoints");
        console("delete watchpoints");
        require(find<QTableWidget>(window, "pointsTable")->rowCount() == 0, "Console can remove watchpoints");
        bool edited = false;
        QTimer::singleShot(0, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dialog) return;
            auto* value = dialog->findChild<QLineEdit*>("cpuRegisterWriteValue");
            if (value) {
                value->setText("0x1122");
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
                edited = true;
            } else dialog->reject();
        });
        QMetaObject::invokeMethod(regs, "cellDoubleClicked", Q_ARG(int, 7), Q_ARG(int, 1));
        require(edited && regs->item(7, 1)->text() == "0x00001122" && regs->item(7, 2)->text() == "0x00000000" &&
                regs->item(7, 1)->background().style() != Qt::NoBrush, "CPU edits target one core and highlight differences");
        require(regs->item(0, 1)->text() == "0x00000000", "Image must start halted");
        console("n"); console("s"); step->trigger();
        require(regs->item(0, 1)->text() == "0x00000001", "Step did not refresh R0");
        require(assembly->item(0, 1)->text() == "0x10000206", "Assembly did not follow PC");
        toggleBreakpoint(assembly);
        require(find<QTableWidget>(window, "pointsTable")->rowCount() == 1, "Breakpoint not listed");
        run->trigger();
        waitUntil([run] { return run->isEnabled(); });
        require(assembly->item(0, 1)->text() == "0x10000206", "Breakpoint did not stop before instruction");
        require(memory->item(0, 1)->text() == "00" && memory->item(0, 2)->text() == "00", "Store executed past breakpoint");
        toggleBreakpoint(assembly);
        run->trigger();
        waitUntil([run] { return run->isEnabled(); });
        require(regs->item(0, 1)->text() == "0x00000005", "Run failed to reach demo BKPT");
        require(memory->item(0, 1)->text() == "05" && memory->item(0, 2)->text() == "00", "Memory view not refreshed");
        find<QAction>(window, "resetAction")->trigger();
        require(regs->item(0, 1)->text() == "0x00000000", "Reset did not restore register view");
        require(memory->item(0, 1)->text() == "00" && memory->item(0, 2)->text() == "00", "Reset did not restore memory view");
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
        require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "Timer must stay stopped while held in reset");
        filter->setText("UART0");
        require(timer->isHidden() && !uart->isHidden(), "Peripheral filter mismatch");
        devices->setCurrentItem(uart);
        require(uart->text(1).startsWith("Partiel") && peripheralRegisters->rowCount() == 13, "UART register model missing");
        require(peripheralRegisters->item(2, 1)->text() == "0x00000090", "UART reset flags missing");
        find<QPushButton>(*catalogPage, "openDeviceWindowButton")->click();
        QPointer<QWidget> uartWindow = find<QWidget>(window, "deviceInspection_UART0");
        require(workspace->indexOf(uartWindow) >= 1 && window.findChildren<QDialog*>().isEmpty(), "Inspect in a tab before opening windows");
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
        const auto rxMeaning = uartFields->item(1, 3)->text();
        require(rxMeaning.contains("FIFO RX vide") && rxMeaning.contains("Oui"), "UART bit meaning missing");
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
        require(timerWindow->isVisible() && uartWindow && workspace->indexOf(uartWindow) >= 1, "Several inspection tabs must coexist");
        auto* timerValues = timerWindow->findChild<QTableWidget*>("peripheralsTable");
        step->trigger(); require(timerValues->item(10, 1)->text() == "0x00000000", "Mini window shows the held timer's reset value");
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
        require(assembly->viewport()->width() >= 220 && memory->isVisible() && ioDock->isVisible(), "Compact desktop retains assembler and both main panels");
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
        require(uartWindow.isNull() && workspace->count() == 1 && window.findChildren<QDialog*>().isEmpty(), "Profile change must remove all old inspectors");
        find<QAction>(window, "peripheralWindowAction")->trigger();
        require(devices->topLevelItemCount() >= 60, "RP2350 catalog not rebuilt");
        const auto timer1 = devices->findItems("TIMER1", Qt::MatchExactly, 0);
        require(timer1.size() == 1, "RP2350 TIMER1 missing");
        devices->setCurrentItem(timer1.front());
        require(peripheralRegisters->rowCount() == 19, "RP2350 timer offsets not described");
        require(peripheralRegisters->item(10, 1)->text() == "Pas d'image", "Old session value survived profile change");
        window.loadDemo();
        require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "RP2350 timer reset value");
        step->trigger(); require(peripheralRegisters->item(10, 1)->text() == "0x00000000", "RP2350 TIMER1 starts held in reset");
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
        require(duplicate.isNull() && workspace->indexOf(sioWindow) >= 1, "Closing duplicate lost the original");
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
        find<QAction>(window, "float_memoryDock")->trigger();
        find<QAction>(window, "float_peripheralDock")->trigger();
        QApplication::processEvents();
        require(memoryDock->isFloating() && ioDock->isFloating() && memoryDock->isAncestorOf(memory),
                "Both panels can float at once with their original widgets");
        find<QAction>(window, "float_memoryDock")->trigger();
        find<QAction>(window, "float_peripheralDock")->trigger();
        window.addDockWidget(Qt::BottomDockWidgetArea, memoryDock);
        window.tabifyDockWidget(memoryDock, ioDock);
        find<QAction>(window, "float_assemblyDock")->trigger();
        window.close();
        MainWindow restored;
        restored.show();
        QApplication::processEvents();
        auto* restoredMemory = find<QDockWidget>(restored, "memoryDock");
        auto* restoredPeripherals = find<QDockWidget>(restored, "peripheralDock");
        require(restored.dockWidgetArea(restoredMemory) == Qt::BottomDockWidgetArea &&
                restored.tabifiedDockWidgets(restoredMemory).contains(restoredPeripherals), "Custom moves and tabs persist across windows");
        auto* restoredAssembly = find<QDockWidget>(restored, "assemblyDock");
        require(restoredAssembly->isFloating(), "Floating state is restored");
        find<QAction>(restored, "overviewAction")->trigger();
        QApplication::processEvents();
        require(restoredMemory->isVisible() && restoredPeripherals->isVisible() &&
                restored.tabifiedDockWidgets(restoredMemory).isEmpty() && !restoredAssembly->isFloating(),
                "Reset restores both visible main panels and reattaches assembly");
        std::cout << "GUI checks passed: dock layout, navigation, execution, devices, tab reuse, detach/gather, contexts and profile cleanup\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI FAIL: " << error.what() << '\n';
        return 1;
    }
}
