#include "picodbg/peripheral_bus.hpp"

namespace picodbg {
namespace {
bool in(uint32_t address, uint32_t base) { return address >= base && address - base < 0x4000u; }
uint32_t atomic(uint32_t old, uint32_t value, uint32_t offset) {
    switch (offset >> 12) { case 0: return value; case 1: return old ^ value; case 2: return old | value; default: return old & ~value; }
}
template<class Action> auto atDevice(uint32_t base, Action action) -> decltype(action()) {
    try { return action(); }
    catch (const Error& error) {
        // Standalone devices report register offsets; the bus supplies the
        // actual target address so diagnostics and fault panes stay precise.
        throw Error(error.kind, error.what(), base + error.address);
    }
}
}
PeripheralBus::PeripheralBus(Chip chip) : chip_(chip), timers_{{Timer(chip, chip == Chip::RP2040 ? 0x40054000 : 0x400b0000), Timer(chip, 0x400b8000)}}, pwm_(chip) { reset(); }
uint32_t PeripheralBus::timerBase(unsigned i) const { return chip_ == Chip::RP2040 ? 0x40054000u : 0x400b0000u + i * 0x8000u; }
uint32_t PeripheralBus::uartBase(unsigned i) const { return chip_ == Chip::RP2040 ? 0x40034000u + i * 0x4000u : 0x40070000u + i * 0x8000u; }
uint32_t PeripheralBus::spiBase(unsigned i) const { return chip_ == Chip::RP2040 ? 0x4003c000u + i * 0x4000u : 0x40080000u + i * 0x8000u; }
uint32_t PeripheralBus::resetBase() const { return chip_ == Chip::RP2040 ? 0x4000c000u : 0x40020000u; }
uint32_t PeripheralBus::watchdogBase() const { return chip_ == Chip::RP2040 ? 0x40058000u : 0x400d8000u; }
unsigned PeripheralBus::resetBit(unsigned kind, unsigned index) const {
    const unsigned bits2040[] = {21, 22, 16, 14}, bits2350[] = {23, 26, 18, 16};
    return (chip_ == Chip::RP2040 ? bits2040[kind] : bits2350[kind]) + index;
}
bool PeripheralBus::held(unsigned kind, unsigned i) const { return reset_ & (1u << resetBit(kind, i)); }
void PeripheralBus::reset() {
    for (auto& timer : timers_) timer.reset();
    for (auto& uart : uarts_) uart.reset();
    for (auto& spi : spis_) spi.reset();
    pwm_.reset();
    scratch_.fill(0); gpioOut_[0] = gpioOut_[1] = gpioOe_[0] = gpioOe_[1] = 0;
    // Direct-ELF startup explicitly bypasses ROM: peripheral reset lines start
    // released. This is not the power-on value of the hardware RESET register.
    reset_ = watchdogSelect_ = 0;
}
bool PeripheralBus::handles(uint32_t address) const {
    if (in(address, pwmBase())) return true;
    if ((address >= 0xd0000010 && address < (chip_ == Chip::RP2040 ? 0xd0000030u : 0xd0000050u)) || in(address, resetBase()) || in(address, watchdogBase())) return true;
    for (unsigned i = 0; i < 2; ++i)
        if (in(address, uartBase(i)) || in(address, spiBase(i)) || ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i)))) return true;
    return false;
}
uint32_t PeripheralBus::read(uint32_t address, bool inspect) {
    if (in(address, pwmBase())) return atDevice(pwmBase(), [&] { return pwm_.read(address - pwmBase()); });
    for (unsigned i = 0; i < 2; ++i) {
        if ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i))) return timers_[i].read(address - timerBase(i), inspect);
        if (in(address, uartBase(i))) return atDevice(uartBase(i), [&] { return uarts_[i].read(address - uartBase(i), inspect); });
        if (in(address, spiBase(i))) return atDevice(spiBase(i), [&] { return spis_[i].read(address - spiBase(i), inspect); });
    }
    if (in(address, resetBase())) {
        switch ((address - resetBase()) & 0xfffu) {
        case 0: return reset_; case 4: return watchdogSelect_;
        case 8: return ~reset_ & (chip_ == Chip::RP2040 ? 0x1ffffffu : 0x1fffffffu);
        }
    }
    if (in(address, watchdogBase())) {
        const auto offset = (address - watchdogBase()) & 0xfffu;
        if (offset >= 0xc && offset < 0x2c) return scratch_[(offset - 0xc) / 4];
    }
    if (address >= 0xd0000010 && address < 0xd0000050) {
        if (chip_ == Chip::RP2040) {
            if (address == 0xd0000010) return gpioOut_[0];
            if (address == 0xd0000020) return gpioOe_[0];
        } else {
            const auto offset = address - 0xd0000010;
            if ((offset & 0x1fu) < 8) return (offset < 0x20 ? gpioOut_ : gpioOe_)[(offset >> 2) & 1u];
        }
    }
    throw Error(ErrorKind::Unsupported, "Registre périphérique non émulé", address);
}
void PeripheralBus::write(uint32_t address, uint32_t value) {
    if (in(address, pwmBase())) { if (!held(3, 0)) atDevice(pwmBase(), [&] { pwm_.write(address - pwmBase(), value); }); return; }
    for (unsigned i = 0; i < 2; ++i) {
        if ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i))) { if (!held(0, i)) timers_[i].write(address - timerBase(i), value); return; }
        if (in(address, uartBase(i))) { if (!held(1, i)) atDevice(uartBase(i), [&] { uarts_[i].write(address - uartBase(i), value); }); return; }
        if (in(address, spiBase(i))) { if (!held(2, i)) atDevice(spiBase(i), [&] { spis_[i].write(address - spiBase(i), value); }); return; }
    }
    if (in(address, resetBase())) {
        const auto offset = address - resetBase();
        const auto mask = chip_ == Chip::RP2040 ? 0x1ffffffu : 0x1fffffffu;
        if ((offset & 0xfffu) == 4) { watchdogSelect_ = atomic(watchdogSelect_, value, offset) & mask; return; }
        if ((offset & 0xfffu) == 0) {
            reset_ = atomic(reset_, value, offset) & mask;
            if (held(3, 0)) pwm_.reset();
            for (unsigned i = 0; i < 2; ++i) {
                if (held(0, i)) timers_[i].reset();
                if (held(1, i)) uarts_[i].reset();
                if (held(2, i)) spis_[i].reset();
            }
            return;
        }
    }
    if (in(address, watchdogBase())) {
        const auto offset = address - watchdogBase(), reg = offset & 0xfffu;
        if (reg >= 0xc && reg < 0x2c) { auto& word = scratch_[(reg - 0xc) / 4]; word = atomic(word, value, offset); return; }
    }
    if (address >= 0xd0000010 && address < (chip_ == Chip::RP2040 ? 0xd0000030u : 0xd0000050u)) {
        const uint32_t offset = address - 0xd0000010;
        const bool pico2 = chip_ == Chip::RP2350;
        const unsigned index = pico2 ? (offset >> 2) & 1u : 0u;
        uint32_t& word = (offset < (pico2 ? 0x20u : 0x10u) ? gpioOut_ : gpioOe_)[index];
        const auto operation = pico2 ? (offset & 0x1fu) / 8u : (offset & 15u) / 4u;
        const uint32_t mask = pico2 ? (index ? 0xff00ffffu : 0xffffffffu) : 0x3fffffffu;
        value &= mask;
        if (operation == 0) word = value; else if (operation == 1) word |= value; else if (operation == 2) word &= ~value; else word ^= value;
        return;
    }
    throw Error(ErrorKind::Unsupported, "Écriture périphérique non émulée", address);
}
void PeripheralBus::advance(uint64_t quanta) {
    // Both sources currently use deterministic virtual units. System cycles
    // are supplied explicitly so a later clock tree can replace this policy.
    for (unsigned i = 0; i < 2; ++i)
        if (!held(0, i) && (i == 0 || chip_ == Chip::RP2350)) timers_[i].advanceClocks(quanta, quanta);
    while (quanta--) {
        if (!held(3, 0)) pwm_.tick();
        for (unsigned i = 0; i < 2; ++i) {
            if (!held(1, i)) uarts_[i].tick();
            if (!held(2, i)) atDevice(spiBase(i), [&] { spis_[i].tick(); });
        }
        bool serialWork = !held(3, 0) && pwm_.running();
        for (unsigned i = 0; i < 2; ++i)
            serialWork = serialWork || (!held(1, i) && uarts_[i].hasWork()) || (!held(2, i) && spis_[i].hasWork());
        if (!serialWork) return;
    }
}
uint64_t PeripheralBus::interrupts() const {
    uint64_t lines = timers_[0].interrupts();
    if (pwm_.interrupts(0)) lines |= uint64_t(1) << (chip_ == Chip::RP2040 ? 4u : 8u);
    if (chip_ == Chip::RP2350 && pwm_.interrupts(1)) lines |= uint64_t(1) << 9;
    if (chip_ == Chip::RP2350) lines |= uint64_t(timers_[1].interrupts()) << 4;
    for (unsigned i = 0; i < 2; ++i) {
        if (uarts_[i].interrupts()) lines |= uint64_t(1) << ((chip_ == Chip::RP2350 ? 33u : 20u) + i);
        if (spis_[i].interrupts()) lines |= uint64_t(1) << ((chip_ == Chip::RP2350 ? 31u : 18u) + i);
    }
    return lines;
}
bool PeripheralBus::hasEvents() const {
    if (!held(3, 0) && pwm_.running()) return true;
    for (unsigned i = 0; i < 2; ++i) {
        if ((i == 0 || chip_ == Chip::RP2350) && !held(0, i) && timers_[i].hasScheduledAlarm()) return true;
        if (!held(1, i) && uarts_[i].hasWork()) return true;
        if (!held(2, i) && spis_[i].hasWork()) return true;
    }
    return false;
}
bool PeripheralBus::receiveUart(unsigned index, uint8_t byte) {
    if (index > 1) throw Error(ErrorKind::InvalidArgument, "UART invalide");
    return !held(1, index) && uarts_[index].receive(byte);
}
}
