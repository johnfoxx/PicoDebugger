#include "picodbg/debugger.hpp"
#include <iostream>
#include <stdexcept>

using namespace picodbg;
namespace {
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
void expect(const Debugger& d, const std::string& expression, uint32_t expected) {
    const uint32_t actual = d.evaluate(expression);
    check(actual == expected, expression + ": expected " + hex(expected) + ", got " + hex(actual));
}
void runTo(Debugger& d, uint32_t address) {
    const Stop stop = d.run(100000);
    check(stop.kind == StopKind::Breakpoint && d.cpus[0].r[15] == address,
          "Expected breakpoint at " + hex(address) + ", got " + stopName(stop.kind) + ": " + stop.message);
}
void peripherals(Debugger& d) {
    check(d.stop.message.find("Boot ROM non exécutée") != std::string::npos,
          "Direct startup must report that the Boot ROM was not executed");
    const Stop stop = d.run(1000);
    check(stop.kind == StopKind::Breakpoint && stop.message == "BKPT 3",
          "Expected BKPT 3, got " + stopName(stop.kind) + ": " + stop.message);
    const bool pico2 = d.board().chip == Chip::RP2350;
    expect(d, "read32(0xd0000010)", 0x02000000);
    check(d.onboardLed() == LedState::On, "Firmware GPIO configuration turns on the onboard LED");
    expect(d, pico2 ? "read32(0x400b003c)" : "read32(0x40054034)", 1);
    expect(d, pico2 ? "read32(0x400a80f4)" : "read32(0x400500a4)", 1);
    const uint32_t time = d.evaluate("read32(0x20000000)");
    check(time >= 80 && time < 100, "Timer polling result outside expected range: " + hex(time));
}
void bootBlink(Debugger& d) {
    check(d.onboardLed() == LedState::Off, "LED starts off");
    expect(d, "read32(data_cookie)", 0); // The loader must not pre-copy .data.
    expect(d, "pc ^ (Reset_Handler & ~1)", 0);
    const uint32_t mainAddress = d.evaluate("main & ~1");
    d.breakpoints.insert(mainAddress);
    runTo(d, mainAddress);
    expect(d, "read32(data_cookie)", 0x5049434f);
    expect(d, "read32(bss_cookie)", 0);
    expect(d, "read32(boot_stage)", 0); // Before the first main instruction.
    expect(d, "read32(0xe000ed08)", 0x10000200);
    d.breakpoints.erase(mainAddress);
    const uint32_t checkpoint = d.evaluate("blink_checkpoint & ~1");
    d.breakpoints.insert(checkpoint);
    for (uint32_t transition = 1; transition <= 4; ++transition) {
        runTo(d, checkpoint);
        expect(d, "read32(boot_stage)", 3);
        expect(d, "read32(boot_error)", 0);
        expect(d, "read32(half_period_ms)", 1);
        expect(d, "read32(led_state)", transition % 2);
        check(d.onboardLed() == (transition % 2 ? LedState::On : LedState::Off), "Onboard LED follows the firmware");
        expect(d, "read32(led_transitions)", transition);
        expect(d, "read32(blink_count)", transition / 2);
        expect(d, "read32(0xd0000010) & 0x02000000", (transition % 2) << 25);
        expect(d, d.board().chip == Chip::RP2350
                   ? "read32(0xd0000030) & 0x02000000"
                   : "read32(0xd0000020) & 0x02000000", 1u << 25);
    }
    d.reset();
    check(d.onboardLed() == LedState::Off, "Reset turns off the onboard LED");
    expect(d, "read32(data_cookie)", 0);
    expect(d, "read32(led_transitions)", 0);
    runTo(d, checkpoint); // The breakpoint survives reset.
    expect(d, "read32(data_cookie)", 0x5049434f);
    expect(d, "read32(led_transitions)", 1);
}
}

// Fixed integration scenarios, linked directly to the core and built only for tests.
int main(int argc, char** argv) {
    try {
        check(argc == 4, "Expected test scenario, chip and ELF path");
        const std::string scenario = argv[1], chip = argv[2];
        check(scenario == "peripherals" || scenario == "boot_blink", "Unknown ELF test scenario");
        check(chip == "rp2040" || chip == "rp2350", "Unknown chip for ELF test");
        Debugger d(chip == "rp2350" ? Chip::RP2350 : Chip::RP2040);
        d.load(argv[3]);
        if (scenario == "peripherals") peripherals(d); else bootBlink(d);
        std::cout << chip << ": " << scenario << " ELF checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
