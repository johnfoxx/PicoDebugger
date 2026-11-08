#include "picodbg/debugger.hpp"
#include "picodbg/peripherals.hpp"
#include "picodbg/timer.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace picodbg;
namespace {
unsigned checks = 0;
void check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void setTime(Timer& timer, uint64_t value) {
    timer.write(4, static_cast<uint32_t>(value)); timer.write(0, static_cast<uint32_t>(value >> 32));
}
void timerTests() {
    Timer t;
    check(t.read(0x28, true) == 0 && t.read(0x2c, true) == 7, "Timer reset values");
    t.write(4, 0xfffffffe); check(t.read(0x28, true) == 0, "Low write must be staged");
    t.write(0, 1); check(t.read(0x24, true) == 1 && t.read(0x28, true) == 0xfffffffe, "High write commits 64 bits");
    t.read(0x0c, false); t.advance(3);
    check(t.read(0x08, true) == 1 && t.read(0x24, true) == 2 && t.read(0x28, true) == 1, "Read latch survives rollover");
    t.read(0x0c, true); check(t.read(0x08, true) == 1, "Inspection cannot alter high latch");
    t.read(0x0c, false); check(t.read(0x08, true) == 2, "CPU low read captures high");
    t.reset(); t.write(0x10, 5); t.write(0x14, 8);
    check(t.read(0x20, true) == 3, "Alarm writes arm their bits");
    t.advance(5); check(t.read(0x34, true) == 1 && t.read(0x20, true) == 2 && t.interrupts() == 0, "Alarm raw IRQ independent of enable");
    t.write(0x38, 1); check(t.interrupts() == 1, "Enable exposes pending raw interrupt");
    t.write(0x34, 1); check(t.interrupts() == 0, "W1C raw interrupt");
    t.write(0x3c, 4); check(t.interrupts() == 4, "Force bypasses enable mask");
    t.write(0x3c, 0); t.write(0x20, 2); t.advance(20);
    check(t.read(0x34, true) == 0, "Disarmed alarm cannot fire");
    t.reset(); setTime(t, 0xfffffffe); t.write(0x10, 1); t.advance(3);
    check(t.read(0x34, true) == 1 && t.read(0x24, true) == 1, "Alarm fires across low-word rollover");
    t.reset(); t.write(0x1c, 123); t.advance(0x200000000ull);
    check(t.read(0x34, true) == 8, "Bulk ticks cannot skip an alarm");
    t.reset(); t.write(0x10, 0); check(t.read(0x34, true) == 1, "Arming on current count matches immediately");
    t.reset(); t.write(0x10, 50); setTime(t, 51);
    check(t.read(0x34, true) == 0 && t.read(0x20, true) == 1, "Setting time does not traverse skipped counts");
    setTime(t, 50); check(t.read(0x34, true) == 1, "Setting time to an armed comparator value fires it");
    t.reset(); t.write(0x30, 1); t.advance(100); check(t.read(0x28, true) == 0, "Explicit pause");
    t.write(0x30, 0); t.advance(100, 1); check(t.read(0x28, true) == 0, "Debug pause core0");
    t.write(0x2c, 0); t.advance(100, 1); check(t.read(0x28, true) == 100, "Debug pause disabled");
    t.write(0x2038, 3); t.write(0x1038, 1); t.write(0x3038, 2);
    check(t.read(0x38, true) == 0, "SET/XOR/CLR register aliases");
    bool rejected = false;
    try { t.write(0x2034, 1); } catch (const Error& e) { rejected = e.kind == ErrorKind::Unsupported; }
    check(rejected, "Unvalidated W1C alias must be rejected");
    t.reset(); t.write(0x10, 8); t.reset(); t.advance(10);
    check(t.read(0x34, true) == 0 && t.read(0x20, true) == 0, "Reset clears alarms and IRQs");
}
void integrationTests() {
    Debugger d; d.loadImage(demoImage());
    d.memory.load(0x10000140, {0x41, 0x02, 0x00, 0x10}); // IRQ0 -> demo handler.
    d.memory.write(0xe000e100, 1, 4);
    d.memory.write(0x4005403c, 1, 4);
    check((d.memory.system(0).pending & 1) && (d.memory.system(1).pending & 1), "Shared timer drives both NVIC contexts");
    d.step(0);
    check((d.cpus[0].xpsr & 0x1ff) == 16 && d.memory.system(0).active == 1, "Timer IRQ enters external exception");
    check(!(d.memory.system(0).pending & 1), "Active level IRQ not redundantly pended");
    d.memory.write(0x4005403c, 0, 4); d.step(0); d.step(0);
    check(!d.cpus[0].handlerMode() && d.memory.system(0).active == 0 && !(d.memory.system(0).pending & 1), "Acknowledge and return avoid spurious retrigger");
    d.reset(); d.memory.load(0x10000140, {0x41, 0x02, 0x00, 0x10});
    d.memory.load(0x10000200, {0x30, 0xbf, 0x00, 0xbe}); // WFI, BKPT
    d.memory.write(0xe000e100, 1, 4); d.memory.write(0x40054038, 1, 4); d.memory.write(0x40054010, 5, 4);
    d.step(0); check(d.cpus[0].sleeping, "WFI before timer alarm");
    d.run(40); check((d.cpus[0].xpsr & 0x1ff) == 16 && !d.cpus[0].sleeping, "Scheduler advances time while WFI waits");
    d.reset();
    d.memory.write(0x40054010, 10, 4); d.addWatchpoint(0x40054028, 4, true, false);
    const auto before = d.revision(); const auto state = d.memory.peek(0x40054028, 4);
    d.memory.peek(0x40054028, 4); check(d.revision() == before && state == 0, "Peripheral inspection does not advance time");
    d.step(0); check(d.memory.peek(0x40054028, 4) == 1, "One virtual tick per quantum, not twice for two cores");
    d.reset();
    d.memory.load(0x10000140, {0x41, 0x02, 0x00, 0x10});
    d.memory.write(0xe000e100, 1, 4); d.memory.write(0x4005403c, 1, 4);
    d.step(0); d.step(0); d.step(0); // Enter, BKPT, exception return; leave source asserted.
    check(!d.cpus[0].handlerMode() && (d.memory.system(0).pending & 1), "Held IRQ level re-pends after return");
    d.step(0); check((d.cpus[0].xpsr & 0x1ff) == 16, "Unacknowledged timer IRQ is delivered again");
}
void catalogTests() {
    for (Chip chip : {Chip::RP2040, Chip::RP2350}) {
        const auto devices = peripheralCatalog(chip);
        check(devices.size() >= (chip == Chip::RP2040 ? 35u : 60u), "Hardware inventory is populated");
        std::set<std::string> names; std::set<uint32_t> bases;
        for (const auto& device : devices) {
            check(names.insert(device.name).second && bases.insert(device.base).second, "Unique peripheral names and bases");
            for (const auto& reg : device.registers) for (const auto& field : reg.fields) fieldValue(0xffffffff, field);
            if (device.name == "TIMER")
                check(device.base == 0x40054000 && device.registers.back().address == 0x40054040, "RP2040 TIMER/INTS mapping");
            if (device.name == "TIMER1")
                check(device.base == 0x400b8000 && device.registers.back().address == 0x400b8048, "RP2350 TIMER1 has different INTS offset");
        }
        const auto profile = describe(chip);
        for (size_t i = 1; i < profile.regions.size(); ++i)
            check(profile.regions[i - 1].end <= profile.regions[i].begin, "Memory map windows must not overlap");
        check(names.count("SIO") && names.count("UART0") && names.count("DMA") && names.count("PIO0"), "Unimplemented devices stay visible");
        if (chip == Chip::RP2350) check(names.count("TIMER1") && names.count("PIO2") && names.count("OTP_DATA") && names.count("SIO_NONSEC"), "RP2350-specific inventory");
    }
    check(fieldValue(0x80000000, {"TOP", 31, 1}) == 1, "Top-bit field extraction");
    check(fieldValue(0xdeadbeef, {"WORD", 0, 32}) == 0xdeadbeef, "32-bit field without shift overflow");
}
}
int main() {
    try { timerTests(); integrationTests(); catalogTests(); std::cout << checks << " peripheral checks passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
