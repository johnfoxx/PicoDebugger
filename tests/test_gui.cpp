#include "main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QPixmap>
#include <QRegion>
#include <QTableWidget>
#include <QThread>
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
            require(view->viewport()->width() >= 40 && view->viewport()->height() >= 30, "Panel viewport collapsed");
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
        if (argc == 2) require(window.grab().save(QString::fromLocal8Bit(argv[1])), "Cannot save GUI screenshot");
        std::cout << "GUI checks passed: simultaneous panels, step, breakpoint, run, reset, layout restore\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GUI FAIL: " << error.what() << '\n';
        return 1;
    }
}
