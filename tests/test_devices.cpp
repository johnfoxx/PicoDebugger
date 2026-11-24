#include "picodbg/debugger.hpp"
#include "picodbg/pwm.hpp"
#include "picodbg/serial.hpp"
#include <iostream>
#include <stdexcept>

using namespace picodbg;
namespace {
unsigned checks = 0;
void check(bool ok, const char* text) { ++checks; if (!ok) throw std::runtime_error(text); }
template<class F> void unsupported(F action) {
    try { action(); } catch (const Error& e) { check(e.kind == ErrorKind::Unsupported, "Expected Unsupported"); return; }
    throw std::runtime_error("Unsupported operation was accepted");
}
void timer2350() {
    Timer t(Chip::RP2350, 0x400b0000);
    t.write(0x38, 1); t.advanceClocks(2, 9); check(t.read(0x28, true) == 9, "SOURCE selects supplied system cycles");
    t.write(0x38, 0); t.advanceClocks(2, 9); check(t.read(0x28, true) == 11, "SOURCE tick selects supplied ticks");
    t.write(0x40, 1); t.write(0x10, 14); t.write(0x34, 1);
    t.write(0x3034, 1); t.write(0x30, 1); t.write(0x10, 99);
    t.advanceClocks(3, 3); check(t.read(0x34, true) == 1 && t.read(0x48, true) == 1, "LOCKED blocks all writes but counting/alarms continue");
    t.write(0x3c, 1); check(t.read(0x48, true) == 1, "LOCKED prevents IRQ acknowledge");
    t.reset(); check(t.read(0x34, true) == 0 && t.read(0x28, true) == 0, "Peripheral reset releases LOCKED");
    Memory m(Chip::RP2350);
    m.write(0x40023000, (1u << 23) | (1u << 24), 4);
    m.write(0x400b0010, 3, 4); m.write(0x400b0040, 1, 4);
    m.write(0x400b8014, 4, 4); m.write(0x400b8040, 2, 4);
    m.write(0x400b0030, 1, 4); m.advanceTime(4);
    check(m.peek(0x400b0028, 4) == 0 && m.peek(0x400b8028, 4) == 4, "Independent RP2350 timers/pause");
    check((m.system(0).pending & 0x21u) == 0x20u && (m.system(1).pending & 0x20u), "TIMER1 IRQ1 maps to line5 on both cores");
    m.write(0x400b0030, 0, 4); m.advanceTime(3); check(m.system(0).pending & 1, "TIMER0 IRQ0 mapping");
}
void onboardLedTests() {
    constexpr uint32_t ledMask = 1u << 25;
    for (const bool pico2 : {false, true}) {
        const uint32_t ctrl = pico2 ? 0x400280ccu : 0x400140ccu;
        const uint32_t pad = pico2 ? 0x40038068u : 0x4001c068u;
        const uint32_t sioOe = pico2 ? 0xd0000030u : 0xd0000020u;
        const uint32_t reset = pico2 ? 0x40020000u : 0x4000c000u;
        const uint32_t resetIo = 1u << (pico2 ? 6 : 5);
        const uint32_t resetPads = 1u << (pico2 ? 9 : 8);
        Debugger d(pico2 ? Board::Pico2 : Board::Pico);
        auto& m = d.memory;
        check(d.onboardLed() == LedState::Off && m.peek(ctrl, 4) == 31, "LED and mux reset state");
        m.write(reset + 0x3000u, resetIo | resetPads | (1u << (pico2 ? 26 : 22)), 4);
        m.write(pico2 ? 0x40070030u : 0x40034030u, 0x301, 4);
        m.write(0xd0000010, ledMask, 4);
        m.write(sioOe, ledMask, 4);
        check(d.onboardLed() == LedState::Off, "UART and SIO latches alone do not control the LED");
        m.write(ctrl, 5, 4);
        if (pico2) {
            check(d.onboardLed() == LedState::Off, "RP2350 pad isolation blocks LED");
            m.write(pad + 0x3000u, 1u << 8, 4);
        }
        check(d.onboardLed() == LedState::On, "SIO drives the active-high LED");
        m.write(pad + 0x2000u, 1u << 7, 4);
        check(d.onboardLed() == LedState::Off, "Pad output disable turns LED off");
        m.write(pad + 0x3000u, 1u << 7, 4);
        const auto outover = pico2 ? 12 : 8;
        const auto oeover = pico2 ? 14 : 12;
        m.write(ctrl, 5u | (1u << outover), 4);
        check(d.onboardLed() == LedState::Off, "OUTOVER can invert the SIO output");
        m.write(ctrl, 5u | (2u << oeover), 4);
        check(d.onboardLed() == LedState::Off, "OEOVER can disable the output");
        m.write(ctrl, 5, 4);
        m.write(sioOe, 0, 4);
        check(d.onboardLed() == LedState::Off, "Releasing SIO OE turns the LED off");
        m.write(sioOe, ledMask, 4);
        m.write(reset + 0x2000u, resetPads, 4);
        m.write(pad, 0, 4);
        check(d.onboardLed() == LedState::Off && m.peek(pad, 4) == (pico2 ? 0x116u : 0x56u), "Pad reset holds the pad at its reset configuration");
        m.write(reset + 0x2000u, resetIo, 4);
        m.write(ctrl, 5, 4);
        m.write(reset + 0x3000u, resetIo | resetPads, 4);
        check(d.onboardLed() == LedState::Off && m.peek(ctrl, 4) == 31, "Mux reset clears the pin selection");
        if (pico2) {
            m.write(pad + 0x3000u, 1u << 8, 4);
            check(!(m.peek(pad, 4) & (1u << 8)), "Firmware can clear ISO after releasing the bank");
        }

        const auto ioBase = ctrl - 0xccu;
        const auto padBase = pad - 0x68u;
        const auto pin = gpioCount(d.board().chip) - 1u;
        const auto pinMask = 1u << (pin % 32u);
        const auto status = ioBase + pin * 8u;
        const auto bit = 1u << (4u * (pin % 8u) + 3u);
        const auto intr = ioBase + gpioIrqOffset(d.board().chip) + 4u * (pin / 8u);
        const auto proc0Enable = intr + 4u * gpioIrqWords(d.board().chip);
        const auto proc0Status = intr + 12u * gpioIrqWords(d.board().chip);
        m.write(status + 4, 5, 4);
        m.write(padBase + 4 + pin * 4u, 0x40, 4);
        m.write(proc0Enable, bit, 4);
        m.write(pico2 ? 0xd000003cu : 0xd0000024u, pinMask, 4);
        m.write(pico2 ? 0xd000001cu : 0xd0000014u, pinMask, 4);
        check(m.peek(status + 4, 4) == 5 && (m.peek(pico2 ? 0xd0000008u : 0xd0000004u, 4) & pinMask),
              "The last pin in either GPIO bank takes configuration and returns its digital level");
        check((m.peek(intr, 4) & bit) && (m.peek(proc0Status, 4) & bit) &&
              (m.system(0).pending & (1u << (pico2 ? 21u : 13u))) && !(m.system(1).pending & (1u << (pico2 ? 21u : 13u))),
              "A GPIO edge and its enable reach only the selected processor");
        m.write(intr, bit, 4);
        check(!(m.peek(proc0Status, 4) & bit), "Acknowledge clears the selected GPIO edge");
        unsupported([&] { m.write(status, 1, 4); });

        Debugger wireless(pico2 ? Board::Pico2W : Board::PicoW);
        wireless.memory.write(reset + 0x3000u, resetIo | resetPads, 4);
        wireless.memory.write(ctrl, 5, 4);
        wireless.memory.write(pad, 0, 4);
        wireless.memory.write(0xd0000010, ledMask, 4);
        wireless.memory.write(sioOe, ledMask, 4);
        check(wireless.onboardLed() == LedState::Unavailable, "GP25 does not report a CYW43 LED state");
    }
}
void uartTests() {
    Uart uart;
    check(uart.read(0x18, true) == 0x90 && uart.read(0x30, true) == 0x300, "UART reset status/control");
    check(uart.read(0x3c, true) == 0, "UART reset must not invent a TX interrupt");
    check(!uart.receive('A'), "Disabled receiver refuses input");
    uart.write(0x2c, 0x70); uart.write(0x30, 0x301);
    uart.write(0, 'A'); check((uart.read(0x18, true) & 8) && uart.output().empty(), "UART TX queued until quantum");
    uart.tick(); check(uart.output() == "A" && (uart.read(0x18, true) & 0x80), "UART TX output and empty flag");
    uart.write(0x30, 0x381); uart.write(0, 'B'); uart.tick();
    check(uart.read(0, true) == 'B' && uart.read(0, true) == 'B', "Inspection does not consume loopback RX");
    check(uart.read(0, false) == 'B' && (uart.read(0x18, true) & 0x10), "CPU read consumes UART RX");
    uart.write(0x38, 0x450); // RX, timeout, overrun.
    for (unsigned i = 0; i < 16; ++i) check(uart.receive(static_cast<uint8_t>(i)), "FIFO RX accepts 16 bytes");
    check(uart.interrupts() & 0x10, "RX half-full IRQ");
    for (unsigned i = 16; i < 32; ++i) uart.receive(static_cast<uint8_t>(i));
    check(!uart.receive(33) && (uart.interrupts() & 0x400), "RX overflow is visible and latched");
    uart.write(0x44, 0x400); uart.write(4, 0);
    check(!(uart.interrupts() & 0x400) && uart.read(4, true) == 0, "Separate ICR and ECR acknowledgement");
    for (unsigned i = 0; i < 32; ++i) check(uart.read(0, false) == i, "FIFO order after overflow");
    uart.receive('Z'); for (unsigned i = 0; i < 32; ++i) uart.tick();
    check(uart.interrupts() & 0x40, "Virtual RX timeout");
    uart.write(0x44, 0x40); check(!(uart.interrupts() & 0x40), "Timeout acknowledge");
    uart.write(0x2038, 0x20); check(uart.read(0x38, true) == 0x470, "UART atomic SET mask");
    unsupported([&] { uart.write(0x34, 7); }); unsupported([&] { uart.write(0x48, 1); });
    uart.reset(); check(uart.output().empty() && uart.read(0x18, true) == 0x90, "UART reset clears queues/output");
    uart.write(0x30, 0x301); uart.receive(0xff);
    check(uart.read(0, false) == 0x1f, "RX honours configured five-bit word length");
    uart.write(0x2c, 0x70); uart.write(0x30, 0x301); uart.write(0x38, 0x20);
    check(!uart.interrupts(), "Enabling an empty UART does not trigger TX IRQ");
    for (unsigned i = 0; i < 17; ++i) uart.write(0, i);
    uart.tick(); check(uart.interrupts() == 0x20, "TX crosses half-full threshold");
    uart.write(0x44, 0x20); uart.tick(); check(!uart.interrupts(), "TX ICR stays cleared until another crossing");
}
void spiTests() {
    Spi spi; check(spi.read(0xc, true) == 3, "SPI empty status");
    spi.write(0, 7); spi.write(0x10, 2); spi.write(4, 3); // 8 bits, master loopback enabled.
    spi.write(8, 0xabcd); check(spi.read(0xc, true) & 16, "SPI busy while queued");
    spi.tick(); check(spi.read(8, true) == 0xcd && spi.read(8, true) == 0xcd, "SPI mask and side-effect-free inspection");
    check(spi.read(8, false) == 0xcd && !(spi.read(0xc, true) & 4), "SPI CPU read consumes RX");
    spi.write(0x14, 7);
    for (unsigned i = 0; i < 9; ++i) { spi.write(8, i); spi.tick(); }
    check((spi.interrupts() & 5) == 5, "SPI overflow and RX half-full IRQ");
    spi.write(0x20, 1); check(!(spi.interrupts() & 1), "SPI overflow W1C");
    unsupported([&] { spi.write(4, 4); }); unsupported([&] { spi.write(0x24, 1); });
    spi.reset(); spi.write(0, 7); spi.write(4, 2); spi.write(8, 1);
    unsupported([&] { spi.tick(); }); // Do not fabricate a connected external SPI device.
}
void pwmTests() {
    for (Chip chip : {Chip::RP2040, Chip::RP2350}) {
        Pwm pwm(chip); const unsigned channels = chip == Chip::RP2040 ? 8u : 12u;
        const auto common = channels * 0x14u;
        check(pwm.read(4) == 16 && pwm.read(16) == 0xffff, "PWM reset divider/top");
        pwm.write(16, 2); pwm.write(common + 8, 1); pwm.write(common, 1);
        pwm.tick(); pwm.tick(); check(pwm.read(8) == 2 && pwm.interrupts(0) == 0, "PWM counts to TOP");
        pwm.write(16, 5); pwm.tick(); check(pwm.read(8) == 0 && pwm.interrupts(0) == 1, "TOP buffered until wrap");
        pwm.write(common + 4, 1); for (unsigned i = 0; i < 5; ++i) pwm.tick();
        check(pwm.read(8) == 5 && !pwm.interrupts(0), "New TOP applied after wrap"); pwm.tick(); check(pwm.interrupts(0) == 1, "Next PWM wrap IRQ");
        pwm.reset(); pwm.write(16, 2); pwm.write(0, 3);
        for (unsigned i = 0; i < 4; ++i) pwm.tick();
        check(pwm.read(8) == 0 && pwm.read(common + 4) == 1, "Phase-correct counts up/down and wraps at zero");
        pwm.reset(); pwm.write(4, 24); pwm.write(0, 1);
        pwm.tick(); check(pwm.read(8) == 0, "Fractional divider first cycle");
        pwm.tick(); pwm.tick(); check(pwm.read(8) == 2, "1.5 divider yields two increments in three cycles");
        if (chip == Chip::RP2350) {
            pwm.write(common + 24, 0x800); check(pwm.interrupts(1) == 0x800, "RP2350 second IRQ and slice11");
        }
        unsupported([&] { pwm.write(0, 0x31); });
    }
}
void busTests() {
    for (Chip chip : {Chip::RP2040, Chip::RP2350}) {
        Memory m(chip); const bool p2 = chip == Chip::RP2350;
        const uint32_t uart = p2 ? 0x40070000 : 0x40034000, resets = p2 ? 0x40020000 : 0x4000c000;
        const unsigned uartBit = p2 ? 26u : 22u, irq = p2 ? 33u : 20u;
        const uint32_t resetMask = p2 ? 0x1fffffffu : 0x01ffffffu;
        const uint32_t uartMask = 1u << uartBit;
        check(m.peek(resets, 4) == resetMask && m.peek(resets + 8, 4) == 0, "All implemented reset lines are asserted at power-on");
        m.write(uart + 0x30, 0x301, 4);
        check(!m.receiveUart(0, 'X'), "A device starts held in reset");
        m.write(resets + 0x3000, uartMask, 4);
        check(m.peek(resets, 4) == (resetMask & ~uartMask) && m.peek(resets + 8, 4) == uartMask,
              "CLR releases only the selected peripheral");
        m.write(uart + 0x30, 0x301, 4); m.write(uart + 0x38, 0x10, 4);
        check(m.receiveUart(0, 'R'), "Bus RX injection");
        check(m.system(0).pending & (uint64_t(1) << irq), "UART line maps to correct NVIC bank");
        if (p2) {
            m.write(0xe000e104, 2, 4); m.write(0xe000e100, 1, 4); m.write(0xe000e180, 1, 4);
            check(m.peek(0xe000e104, 4) == 2 && m.peek(0xe000e204, 4) == 2, "Low bank operations preserve high NVIC bank");
            check(m.peek(0xe000e104, 4, 1) == 0, "NVIC enable is per-core");
            m.write(0xd0000014, 0xffffffff, 4); check(m.peek(0xd0000014, 4) == 0xff00ffff, "RP2350 GPIO high mask");
            m.write(0xd0000018, 3, 4); m.write(0xd0000020, 1, 4);
            check(m.peek(0xd0000010, 4) == 2, "RP2350 GPIO aliases differ from RP2040");
            m.write(0xd0000038, 2, 4); check(m.peek(0xd0000030, 4) == 2, "RP2350 GPIO OE mapping");
        }
        m.write(resets + 0x2000, uartMask, 4);
        check(m.peek(uart + 0x30, 4) == 0x300 && !(m.peek(resets + 8, 4) & uartMask), "Peripheral reset resets UART and clears DONE");
        m.write(uart + 0x30, 0x301, 4); check(!m.receiveUart(0, 'X'), "Held reset ignores writes/input");
        m.write(resets + 0x3000, uartMask, 4); check(m.peek(resets + 8, 4) & uartMask, "Reset deassert sets DONE");
        const auto wd = p2 ? 0x400d8000u : 0x40058000u;
        m.write(wd + 0x28, 0x12345678, 4); check(m.peek(wd + 0x28, 4) == 0x12345678, "Watchdog scratch7");
        unsupported([&] { m.write(wd, 0x40000000, 4); });
        bool diagnosed = false;
        try { m.write(uart + 0x48, 1, 4); }
        catch (const Error& e) { diagnosed = e.kind == ErrorKind::Unsupported && e.address == uart + 0x48; }
        check(diagnosed, "Device diagnostics use absolute bus addresses");
    }
    Debugger d(Chip::RP2350); d.loadImage(demoImage());
    d.memory.write(0x40023000, 1u << 26, 4);
    d.memory.write(0x40070030, 0x301, 4);
    const auto revision = d.revision(); check(d.injectUart(0, "RX") == 1 && d.revision() > revision, "Injection reports FIFO capacity and updates snapshot revision");
    d.reset();
    check(d.peek(0x40020000, 4) == 0x1fffffffu && d.peek(0x40020008, 4) == 0, "Debugger reset restores startup reset lines");
}
}
int main() {
    try { timer2350(); onboardLedTests(); uartTests(); spiTests(); pwmTests(); busTests(); std::cout << checks << " device checks passed\n"; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
