#include "picodbg/debugger.hpp"
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace picodbg;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const Error&) { return; }
    throw std::runtime_error("Expected a rejected operation");
}
void boards() {
    for (const auto board : {Board::Pico, Board::PicoW, Board::Pico2, Board::Pico2W}) {
        Debugger d(board); const auto& info = d.board();
        const bool wireless = board == Board::PicoW || board == Board::Pico2W;
        const bool pico2 = board == Board::Pico2 || board == Board::Pico2W;
        Board parsed;
        check(parseBoard(info.id, parsed) && parsed == board, "Board argument round trip");
        check(info.wireless == wireless && info.chip == (pico2 ? Chip::RP2350 : Chip::RP2040), "Board must select the right SoC");
        check(info.flashSize == d.memory.description().flashSize && info.flashSize == (pico2 ? 4194304u : 2097152u), "Board flash capacity");
        check(d.memory.description().ramSize == (pico2 ? 532480u : 270336u), "Board RAM capacity");
        check(info.connections.front().connection == (wireless ? "CYW43 · WL_GPIO0" : "GPIO25"), "LED wiring must not confuse CS and LED");
        d.loadImage(demoImage()); d.step(0); d.reset();
        check(d.board().board == board && d.virtualTime() == 0, "Reset preserves the board, clears time");
        const auto baseMap = describe(info.chip);
        check(baseMap.regions.size() == d.memory.description().regions.size(), "Wi-Fi must not add invented SoC MMIO");
        for (size_t i = 0; i < baseMap.regions.size(); ++i)
            check(baseMap.regions[i].begin == d.memory.description().regions[i].begin, "Wi-Fi must not shift MCU addresses");
        const uint32_t timer = pico2 ? 0x400b0000 : 0x40054000;
        d.advanceTime(3);
        check(d.memory.peek(timer + 0x28, 4) == 0, "Timer remains stopped until released");
        d.memory.write(pico2 ? 0x40023000 : 0x4000f000, 1u << (pico2 ? 23 : 21), 4);
        d.advanceTime(9); check(d.memory.peek(timer + 0x28, 4) == 9, "Board uses correct timer model");
        const auto revision = d.revision();
        unsigned cpuAccesses = 0;
        d.memory.onAccess = [&](const MemoryAccess&) { ++cpuAccesses; };
        d.writeMemory(0x20000000, 0x1234, 2);
        d.writePeripheralRegister(0xd0000010, 0x1234);
        d.writePeripheralRegister(0xe000e014, 10, 1);
        d.writePeripheralRegister(0xd0000054, 42, 0);
        check(d.peek(0x20000000, 4) == 0x1234 && d.peek(0xd0000010, 4) == 0x1234, "Debugger writes RAM and GPIO through the bus");
        check(d.peek(0xe000e014, 4, 0) == 0 && d.peek(0xe000e014, 4, 1) == 10, "Debugger write selects the intended core");
        check(d.peek(0xd0000058, 4, 1) == 42 && d.peek(0xd0000058, 4, 1) == 42, "Watching FIFO must not consume it");
        rejected([&] { d.writePeripheralRegister(0xd0000000, 0); });
        rejected([&] { d.writeMemory(0xd0000010, 0, 4); });
        rejected([&] { d.writeMemory(0x10000000, 0, 4); });
        rejected([&] { d.writeMemory(0x20000000, 0x100, 1); });
        check(d.revision() == revision + 4 && cpuAccesses == 0, "Debugger writes notify views without triggering CPU accesses");
    }
    Board board = Board::Pico;
    check(!parseBoard("pico3", board), "Unknown board rejected");
    check(Debugger(Chip::RP2350).board().board == Board::Pico2, "Old chip API remains compatible");
}
void stacks() {
    Debugger debugger;
    debugger.loadImage(demoImage());
    debugger.startCore(1, 0x10000201, 0x20042000);
    debugger.memory.load(0x10000200, {0x00, 0xf0, 0x02, 0xf8}); // BL 0x10000208.
    debugger.memory.load(0x10000208, {0x80, 0xb5, 0x80, 0xbd}); // PUSH {r7, lr}; POP {r7, pc}.
    debugger.writeCpuRegister(1, 7, 0x11223344);
    check(debugger.registerValue(1, 7) == 0x11223344 && debugger.registerValue(0, 7) == 0, "Register edits select a core");
    debugger.step(1);
    debugger.step(1);
    const auto frames = debugger.backtrace(1);
    check(frames.size() == 2 && frames[0].pc == 0x1000020a && frames[1].pc == 0x10000200 &&
          frames[0].sp + 8 == frames[1].sp && debugger.backtrace(0).size() == 1, "Calls show the current and caller frames per core");
    debugger.step(1);
    check(debugger.backtrace(1).size() == 1 && debugger.registerValue(1, 15) == 0x10000204, "Return removes the observed caller");
    debugger.writeCpuRegister(1, 17, 0x20000402);
    check(debugger.registerValue(1, 13) == 0x20000400, "Editing the active stack bank updates SP");
    rejected([&] { debugger.writeCpuRegister(2, 0, 1); });
    debugger.reset();
    rejected([&] { debugger.backtrace(1); });
}
void time() {
    Debugger d;
    rejected([&] { d.advanceTime(1); }); rejected([&] { d.runFor(1); });
    d.loadImage(demoImage());
    d.memory.write(0x4000f000, 1u << 21, 4);
    const auto pc = d.cpus[0].r[15]; const auto revision = d.revision();
    d.memory.write(0x40054010, 5, 4); d.memory.write(0x40054038, 1, 4);
    d.memory.write(0xe000e014, 3, 4); d.memory.write(0xe000e010, 7, 4);
    d.addWatchpoint(0x40054028, 4, true, false);
    d.advanceTime(5);
    check(d.virtualTime() == 5 && d.memory.peek(0x40054028, 4) == 5, "Manual advance shares virtual clock");
    check(d.cpus[0].r[15] == pc && d.cpus[0].instructions == 0 && d.cpus[1].held, "Manual time freezes CPUs");
    check((d.memory.system(0).pending & 1) && (d.memory.system(1).pending & 1), "Alarm latched on both cores without handler execution");
    check(d.memory.system(0).systickPending && d.cpus[0].xpsr == 0x01000000, "SysTick advances without taking exception");
    check(d.revision() > revision && d.stop.kind == StopKind::User, "Advance creates a fresh snapshot, no memory watch hit");
    rejected([&] { d.advanceTime(1000001); });
    rejected([&] { d.runFor(std::numeric_limits<uint64_t>::max()); });
    check(d.virtualTime() == 5, "Invalid duration does not mutate state");
    d.memory.write(0x40054030, 1, 4); d.advanceTime(2);
    check(d.virtualTime() == 7 && d.memory.peek(0x40054028, 4) == 5, "Peripheral PAUSE does not stop global elapsed time");
    auto bad = demoImage(); bad.hasVectors = false; bad.entry = 0;
    rejected([&] { d.loadImage(bad); });
    check(d.virtualTime() == 7, "Rejected ELF preserves virtual time");
    d.reset(); d.clearWatchpoints();
    check(d.virtualTime() == 0, "Reset resets clock");
    check(d.runFor(3, 100).kind == StopKind::User && d.virtualTime() == 3 && d.cpus[0].instructions == 3, "Run duration counts executed quanta, not skipped core slots");
    check(d.cpus[0].r[0] == 1, "Timed run executes firmware");
    d.breakpoints.insert(d.cpus[0].r[15]);
    check(d.runFor(20, 100).kind == StopKind::Breakpoint && d.virtualTime() == 3, "Breakpoint stops before time advances");
    d.advanceTime(4);
    check(d.runFor(1, 100).kind == StopKind::User && d.virtualTime() == 8, "Manual time preserves breakpoint resume token");
    check(d.memory.peek(0x20000000, 4) == 1, "Resume executes stopped store exactly once");
    d.breakpoints.clear(); d.reset(); d.startCore(1, 0x10000221, 0x20042000);
    check(d.runFor(6, 100).kind == StopKind::User && d.virtualTime() == 6, "Shared clock with two active CPUs");
    check(d.cpus[0].instructions == 3 && d.cpus[1].instructions == 3, "Timed scheduling remains deterministic");
    d.reset(); d.memory.load(0x10000200, {0xfe, 0xe7}); // Infinite B to self.
    check(d.runFor(1000, 5).kind == StopKind::Budget && d.virtualTime() < 1000, "Timed operation yields to caller");
    d.pause(); const auto t = d.virtualTime();
    d.memory.peek(0x40054028, 4); check(d.virtualTime() == t, "Pause/inspection cannot advance time");
    d.reset(); d.memory.load(0x10000200, {0x30, 0xbf});
    check(d.runFor(100, 1000).kind == StopKind::Idle && d.virtualTime() == 1, "WFI without an event stops early");
    d.reset(); d.cpus[0].held = true;
    check(d.runFor(100, 1000).kind == StopKind::Idle && d.virtualTime() == 0, "Held cores cannot cause an endless timed run");
    d.reset(); d.memory.load(0x10000200, {0x18, 0xbf});
    check(d.runFor(100).kind == StopKind::Unsupported && d.virtualTime() == 0, "Unsupported instruction stops timed execution");
    rejected([&] { d.advanceTime(1); });
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "boards") boards(); else { time(); stacks(); }
        std::cout << "Session checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
