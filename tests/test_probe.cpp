#include "gdb_target.hpp"
#include "main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace picodbg;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void waitUntil(const std::function<bool()>& condition, GdbTarget* target = nullptr) {
    QElapsedTimer timer; timer.start();
    do {
        QApplication::processEvents(); if (target) target->poll();
        if (condition()) return;
        QThread::msleep(2);
    } while (timer.elapsed() < 7000);
    throw std::runtime_error("Timed out waiting for GDB state: " + (target ? target->errorText().toStdString() : "UI"));
}
void ready(GdbTarget& target) { waitUntil([&] { return target.loaded() && !target.busy() && !target.executing() && target.registerAvailable(0, 15); }, &target); }
void rejected(const std::function<void()>& action) { try { action(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
QByteArray commands() { QFile file(QString::fromLocal8Bit(qgetenv("PICODBG_TEST_GDB_LOG"))); check(file.open(QIODevice::ReadOnly), "Read command log"); return file.readAll(); }
void mode(const char* value) { qputenv("PICODBG_TEST_GDB_MODE", value); }
void parserTests() {
    const auto r = parseMi("42^done,threads=[{id=\"1\",name=\"core\\\"0\\n\\303\\251\"}],other={a=\"x\",a=\"y\"}");
    check(r.token == 42 && r.klass == "done", "MI token/result parsing");
    check(r.data.get("threads").items.at(0).get("name").text == QString::fromUtf8("core\"0\né"), "C string escapes including octal UTF-8");
    check(r.data.get("other").items.size() == 2, "Repeated MI keys preserved");
    const auto quoted = miQuote(QString::fromUtf8("a \\\"\n é"));
    check(parseMi(("~" + quoted).toUtf8()).data.text == QString::fromUtf8("a \\\"\n é"), "MI quoting round trip");
    rejected([] { parseMi("1^done,a={b=\"truncated"); });
    rejected([] { parseMi("1^done,a=\"x\"trailing"); });
    check(parseMi("(gdb) ").kind == 0, "Prompt ignored");
}
void transportTests(const QString& fake) {
    GdbMi mi; bool started = false, failed = false;
    mi.started = [&] { started = true; }; mi.failed = [&](const QString&) { failed = true; };
    mi.start(fake); waitUntil([&] { return started; });
    mi.command("-test-timeout", [](const MiRecord&) { throw std::runtime_error("Unexpected reply"); }, 40);
    waitUntil([&] { return failed; }); check(!mi.active(), "Timeout terminates only owned GDB");
    started = failed = false; mi.start(fake); waitUntil([&] { return started; });
    mi.command("-test-unknown-token", [](const MiRecord&) {});
    waitUntil([&] { return failed; }); check(!mi.active(), "Unmatched replies cannot corrupt session state");
}
void targetTests(const ProbeOptions& options, const std::string& elf) {
    mode(""); GdbTarget target(Board::Pico); target.connectProbe(options); ready(target);
    check(target.hardware() && target.cpus[0].r[15] == 0x10000200 && target.cpus[1].r[0] == 100, "Actual per-core snapshots");
    check(!target.registerAvailable(0, 18) && target.extraRegisters()[0].count("s0"), "Unavailable and extended registers distinguished");
    check(!commands().contains("-target-download"), "Connection must not flash anything");
    target.load(elf); ready(target); check(target.image.symbols.count("counter"), "ELF symbols loaded for shared evaluator");
    check(!commands().contains("-target-download"), "Loading symbols must not program target");
    rejected([&] { target.peek(0x20000000, 4); });
    waitUntil([&] { try { return target.peek(0x20000000, 4) == 0x12345678; } catch (const Error&) { return false; } }, &target);
    check(target.evaluate("read32(counter) + r0") == 0x12345678, "Watch uses target memory, never simulated memory");
    const auto before = commands();
    rejected([&] { target.peek(0x40034000, 4); }); // UARTDR consumes RX.
    rejected([&] { target.peek(0xd0000100, 4); }); // SIO spinlock acquires a lock.
    rejected([&] { target.peek(0xe000e010, 4); }); // SysTick CSR clears COUNTFLAG.
    check(commands() == before, "Automatic inspection must not read sensitive registers");
    target.readRegisterOnce(0x40034000, 0);
    waitUntil([&] { try { return target.peek(0x40034000, 4) == 0x12345678; } catch (const Error&) { return false; } }, &target);
    const auto manual = commands(); target.peek(0x40034000, 4); check(commands() == manual, "One explicit read reused for fields and table");
    rejected([&] { target.peek(0x20001000, 4); });
    waitUntil([&] {
        try { target.peek(0x20001000, 4); } catch (const Error& e) { return std::string(e.what()) == "Cannot access memory"; }
        return false;
    }, &target);
    const auto failedRead = commands();
    rejected([&] { target.peek(0x20001000, 4); });
    check(commands() == failedRead, "Unreadable memory is cached as an error without repeated I/O");
    rejected([&] { target.disassemble(0x10000200, 0); });
    waitUntil([&] { try { return target.disassemble(0x10000200, 0).text == "nop"; } catch (const Error&) { return false; } }, &target);
    target.step(1); ready(target); check(target.cpus[1].r[15] == 0x10000222, "Step selects physical core context");
    target.run(0); waitUntil([&] { return target.executing() && !target.busy(); }, &target);
    check(!target.registerAvailable(0, 0), "Running register values unavailable");
    rejected([&] { target.peek(0x20000000, 4); }); rejected([&] { target.download(false); });
    target.pause(); ready(target); check(target.stop.kind == StopKind::User, "Interrupt waits for stop notification");
    target.addBreakpoint(0x10000204); ready(target); check(target.breakpoints.count(0x10000204), "Confirmed hardware breakpoint exposed");
    target.run(0); ready(target); check(target.stop.kind == StopKind::Breakpoint, "Async breakpoint stop preserved");
    target.removeBreakpoint(0x10000204); ready(target); check(target.breakpoints.empty(), "Remove real breakpoint");
    target.addWatchpoint(0x20000000, 4, true, true); ready(target); check(target.watchpoints.size() == 1, "Hardware watchpoint verified");
    rejected([&] { target.addWatchpoint(0x20000001, 4, false, true); });
    target.removeWatchpoint(0); ready(target); check(target.watchpoints.empty(), "Remove real watchpoint");
    rejected([&] { target.download(true); }); // Flash fixture cannot be silently treated as RAM.
    target.download(false); ready(target); check(commands().contains("-target-download"), "Explicit download reaches GDB");
    target.reset(); ready(target);
    rejected([&] { target.advanceTime(1); }); rejected([&] { target.injectUart(0, "x"); });
    target.disconnectProbe(); waitUntil([&] { return !target.connected() && !target.busy(); }, &target);
    check(!target.registerAvailable(0, 15), "Disconnect invalidates snapshots");
    target.connectProbe(options); ready(target);
    check(target.elfPath().isEmpty() && target.image.symbols.empty(), "A fresh GDB connection starts without ELF symbols");
    rejected([&] { target.download(false); });
    target.disconnectProbe(); waitUntil([&] { return !target.connected(); }, &target);
}
void ramTests(const ProbeOptions& options, const std::string& ram, const std::string& invalid) {
    mode(""); GdbTarget target(Board::Pico); target.connectProbe(options); ready(target);
    target.load(invalid); ready(target);
    const auto before = commands(); rejected([&] { target.download(true); });
    check(commands() == before, "Invalid RAM vectors rejected before any target write");
    target.load(ram); ready(target); rejected([&] { target.download(false); });
    const auto offset = commands().size(); target.download(true); ready(target);
    const auto sequence = commands().mid(offset);
    check(sequence.count("monitor reset halt") == 1 && sequence.indexOf("monitor reset halt") < sequence.indexOf("-target-download"),
          "RAM upload resets only before loading");
    check(sequence.contains("-data-write-register-values --thread 1 x 13 0x20041000") &&
          sequence.contains("-data-write-register-values --thread 1 x 15 0x20000200") &&
          sequence.contains("-data-write-register-values --thread 1 x 16 0x01000000") && sequence.contains("-data-write-memory-bytes 0xe000ed08 00010020"),
          "RAM entry installs SP, Thumb PC, xPSR and little-endian VTOR");
    check(!sequence.contains("-exec-continue") && !target.executing(), "Upload leaves target halted");
    check(target.cpus[0].r[15] == 0x20000200, "RAM upload reads the new physical PC back");
    target.disconnectProbe(); waitUntil([&] { return !target.connected(); }, &target);
}
void serverTests(ProbeOptions options, const QString& server) {
    mode(""); options.launchOpenOcd = true; options.openOcd = server; options.port = 3344;
    options.scripts = "/test/scripts with spaces";
    GdbTarget target(Board::Pico); target.connectProbe(options); ready(target);
    QFile log(QString::fromLocal8Bit(qgetenv("PICODBG_TEST_SERVER_LOG"))); check(log.open(QIODevice::ReadOnly), "OpenOCD arguments log");
    const auto args = log.readAll(); log.close();
    check(args.contains("interface/cmsis-dap.cfg") && args.contains("target/rp2040.cfg") && args.contains("gdb_port 3344") &&
          args.contains("/test/scripts with spaces"), "Official probe config and spaced paths passed as separate arguments");
    target.disconnectProbe(); waitUntil([&] { return !target.connected(); }, &target);
    qputenv("PICODBG_TEST_SERVER_MODE", "fail"); target.connectProbe(options);
    waitUntil([&] { return !target.errorText().isEmpty(); }, &target);
    check(!target.connected() && !target.busy(), "Missing probe fails without a hanging GUI");
    qputenv("PICODBG_TEST_SERVER_MODE", "no_port"); target.connectProbe(options);
    target.disconnectProbe(); check(!target.connecting() && !target.busy(), "Pending connection can be cancelled");
    qunsetenv("PICODBG_TEST_SERVER_MODE");
    target.connectProbe(options); ready(target); target.disconnectProbe();
    waitUntil([&] { return !target.connected(); }, &target);
}
void failureTests(const ProbeOptions& options, const std::string& elf) {
    for (const auto value : {"wrong_chip", "unmapped"}) {
        mode(value); GdbTarget target(Board::Pico); target.connectProbe(options);
        waitUntil([&] { return !target.errorText().isEmpty(); }, &target);
        check(!target.loaded() && !target.connected(), "Wrong chip/unmapped cores cannot be programmed");
        rejected([&] { target.download(false); });
    }
    for (const auto value : {"no_slots", "software_watch", "run_error", "drop", "download_error"}) {
        mode(value); GdbTarget target(Board::Pico); target.connectProbe(options); ready(target);
        if (std::string(value) == "no_slots") target.addBreakpoint(0x10000200);
        else if (std::string(value) == "software_watch") target.addWatchpoint(0x20000000, 4, false, true);
        else if (std::string(value) == "run_error") target.run(0);
        else if (std::string(value) == "drop") rejected([&] { target.peek(0x20000000, 4); });
        else { target.load(elf); ready(target); target.download(false); }
        waitUntil([&] { return !target.errorText().isEmpty(); }, &target);
        check(target.breakpoints.empty() && target.watchpoints.empty(), "Rejected resources never appear active");
        check(!target.executing(), "Failed operation does not pretend to run");
        if (std::string(value) == "download_error") {
            check(target.terminalStop(), "Failed download must block execution of a partial image");
            rejected([&] { target.run(0); });
        }
        if (std::string(value) == "drop") check(!target.connected() && !target.registerAvailable(0, 0), "Link loss invalidates data");
    }
    {
        mode("symbols_error"); GdbTarget target(Board::Pico); target.connectProbe(options); ready(target);
        target.load(elf); ready(target);
        target.load(elf);
        waitUntil([&] { return !target.errorText().isEmpty(); }, &target);
        check(target.elfPath().isEmpty() && target.image.symbols.empty(), "A failed GDB symbol load clears the upload selection");
        rejected([&] { target.download(false); });
    }
    mode("wrong_arch");
    GdbTarget riscv(Board::Pico2);
    riscv.connectProbe(options);
    waitUntil([&] { return riscv.errorText().contains("registres ARM"); }, &riscv);
    check(!riscv.loaded(), "An RP2350 with a RISC-V register set is rejected");
    mode("rp2350"); GdbTarget pico2(Board::Pico2W); pico2.connectProbe(options); ready(pico2);
    check(pico2.connectionText().contains("RP2350") && pico2.board().wireless, "Selected W board distinguished from verified MCU");
    pico2.disconnectProbe(); waitUntil([&] { return !pico2.connected(); }, &pico2); mode("");
}
template<class T> T* find(QObject& parent, const char* name) { auto* object = parent.findChild<T*>(name); check(object != nullptr, name); return object; }
void guiTests(const ProbeOptions& options, const QString& elf, const QString& ramElf) {
    MainWindow window; window.resize(1280, 800); window.loadFile(elf); window.show(); window.connectHardware(options);
    auto* run = find<QAction>(window, "runAction"); auto* download = find<QAction>(window, "downloadElfAction");
    waitUntil([&] { return run->isEnabled() && download->isEnabled(); });
    check(find<QLabel>(window, "targetModeStatus")->text().contains("CARTE RÉELLE"), "Mode status is persistent");
    check(!find<QComboBox>(window, "chipSelector")->isEnabled(), "Board cannot change while connected");
    auto* registers = find<QTableWidget>(window, "registersTable");
    check(registers->item(18, 1)->text() == "—" && registers->rowCount() > 21, "GUI shows missing and extended registers honestly");
    check(!find<QPushButton>(window, "readHardwareRegisterOnce")->isHidden(), "Explicit hardware inspection available");
    check(!find<QAction>(window, "virtualTimeViewAction")->isEnabled(), "Virtual time unavailable on a physical board");
    auto* expression = find<QLineEdit>(window, "consoleExpression");
    expression->setText("print read32(0x20002000)"); QMetaObject::invokeMethod(expression, "returnPressed");
    waitUntil([&] { return find<QPlainTextEdit>(window, "consoleView")->toPlainText().contains("read32(0x20002000) = 0x12345678"); });
    find<QLineEdit>(window, "memoryAddress")->setText("0x40008000");
    find<QPushButton>(window, "readHardwareAddressOnce")->click();
    waitUntil([&] {
        auto* memory = find<QTableWidget>(window, "memoryTable");
        return memory->rowCount() && memory->item(0, 1)->text().startsWith("78 56 34 12");
    });
    if (!qgetenv("PICODBG_TEST_SCREENSHOT").isEmpty())
        check(window.grab().save(QString::fromLocal8Bit(qgetenv("PICODBG_TEST_SCREENSHOT"))), "Save optional GUI capture");
    find<QAction>(window, "stepAction")->trigger(); waitUntil([&] { return registers->item(15, 1)->text() == "0x10000202" && run->isEnabled(); });
    run->trigger(); waitUntil([&] { return find<QAction>(window, "pauseAction")->isEnabled(); });
    check(registers->item(0, 1)->text() == "—", "GUI does not show cached CPU state as live");
    find<QAction>(window, "pauseAction")->trigger(); waitUntil([&] { return run->isEnabled(); });
    const auto beforeLaunch = commands().size();
    bool confirmed = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        if (buttons) {
            confirmed = true;
            buttons->button(QDialogButtonBox::Ok)->click();
        } else dialog->reject();
    });
    window.launchProjectElf(elf, Board::Pico, true, true);
    check(confirmed, "IDE board launch asks for flash or RAM confirmation");
    waitUntil([&] { return find<QAction>(window, "pauseAction")->isEnabled(); });
    const auto launch = commands().mid(beforeLaunch);
    const int symbols = launch.indexOf("-file-exec-and-symbols");
    const int downloadAt = launch.indexOf("-target-download");
    const int continueAt = launch.indexOf("-exec-continue");
    check(symbols >= 0 && symbols < downloadAt && downloadAt < continueAt, "IDE opens, downloads and runs using the existing SWD session");
    window.showIde();
    QWidget* ide = nullptr;
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "picoIdeWindow") ide = widget;
    check(ide, "IDE window is visible");
    auto* pause = find<QAction>(*ide, "idePause");
    waitUntil([&] { return pause->isEnabled(); });
    const auto beforePause = commands().size();
    pause->trigger();
    waitUntil([&] { return run->isEnabled() && !pause->isEnabled(); });
    check(commands().mid(beforePause).contains("-exec-interrupt"), "IDE requests a hardware pause via GDB");
    check(find<QLabel>(*ide, "ideTargetStatus")->text().contains("arrêtée"), "IDE reports a confirmed SWD pause");
    const auto beforePausedLaunch = commands().size();
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog) dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.launchProjectElf(elf, Board::Pico, true, false);
    waitUntil([&] {
        return find<QPlainTextEdit>(*ide, "ideBuildOutput")->toPlainText().contains("ELF téléversé. Carte arrêtée.");
    });
    const auto pausedLaunch = commands().mid(beforePausedLaunch);
    check(pausedLaunch.contains("-target-download") && !pausedLaunch.contains("-exec-continue") && run->isEnabled(),
          "Default board launch downloads the firmware and leaves the target paused");
    bool ramDefault = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* startup = dialog->findChild<QComboBox*>("downloadStartup");
        ramDefault = startup && startup->currentIndex() == 1;
        dialog->reject();
    });
    window.launchProjectElf(ramElf, Board::Pico, true, true);
    check(ramDefault, "An ELF linked in SRAM defaults to RAM transfer");
    ide->close();
    find<QAction>(window, "emulationModeAction")->trigger();
    waitUntil([&] { return find<QLabel>(window, "targetModeStatus")->text().contains("ÉMULATION"); });
    check(find<QComboBox>(window, "chipSelector")->isEnabled(), "Return to emulation restores board choice");
    check(registers->item(15, 1)->text() == "0x10000200", "Original ELF restored in emulator"); window.close();
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QApplication::setOrganizationName("PicoDebuggerTests"); QApplication::setApplicationName("ProbeTests");
    try {
        check(argc == 6, "Expected fake processes and ELF fixture paths"); mode(""); parserTests(); transportTests(QString::fromLocal8Bit(argv[1]));
        ProbeOptions options; options.gdb = QString::fromLocal8Bit(argv[1]); options.launchOpenOcd = false;
        targetTests(options, argv[2]); failureTests(options, argv[2]);
        guiTests(options, QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
        ramTests(options, argv[3], argv[4]); serverTests(options, QString::fromLocal8Bit(argv[5]));
        std::cout << "GDB/MI transport, hardware target and shared GUI checks passed (test peer, no physical probe)\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
