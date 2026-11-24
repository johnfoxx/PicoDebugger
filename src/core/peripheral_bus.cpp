#include "picodbg/peripheral_bus.hpp"
#include "picodbg/chip.hpp"

namespace picodbg {
namespace {
constexpr uint32_t gpioCtrlReset = 0x1fu;

bool in(uint32_t address, uint32_t base) { return address >= base && address - base < 0x4000u; }
bool inRegisters(uint32_t address, uint32_t base, uint32_t size) {
    return in(address, base) && ((address - base) & 0xfffu) < size;
}
uint32_t atomic(uint32_t old, uint32_t value, uint32_t offset) {
    switch (offset >> 12) { case 0: return value; case 1: return old ^ value; case 2: return old | value; default: return old & ~value; }
}
bool gpioOverride(bool signal, uint32_t mode) {
    switch (mode & 3u) {
    case 0: return signal;
    case 1: return !signal;
    case 2: return false;
    default: return true;
    }
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
uint32_t PeripheralBus::resetMask() const { return chip_ == Chip::RP2040 ? 0x01ffffffu : 0x1fffffffu; }
uint32_t PeripheralBus::watchdogBase() const { return chip_ == Chip::RP2040 ? 0x40058000u : 0x400d8000u; }
uint32_t PeripheralBus::ioBankBase() const { return chip_ == Chip::RP2040 ? 0x40014000u : 0x40028000u; }
uint32_t PeripheralBus::padsBankBase() const { return chip_ == Chip::RP2040 ? 0x4001c000u : 0x40038000u; }
uint32_t PeripheralBus::ioBankResetMask() const { return chip_ == Chip::RP2040 ? 1u << 5 : 1u << 6; }
uint32_t PeripheralBus::padsBankResetMask() const { return chip_ == Chip::RP2040 ? 1u << 8 : 1u << 9; }
uint32_t PeripheralBus::gpioPadReset() const { return chip_ == Chip::RP2040 ? 0x56u : 0x116u; }
uint32_t PeripheralBus::gpioStatus(unsigned pin) const {
    if (reset_ & ioBankResetMask()) return 0;
    const auto ctrl = gpioCtrl_[pin];
    const auto pad = gpioPad_[pin];
    const bool sio = (ctrl & 0x1fu) == 5u;
    const bool pico2 = chip_ == Chip::RP2350;
    const auto mask = 1u << (pin % 32u);
    const bool fromOutput = sio && (gpioOut_[pin / 32u] & mask);
    const bool fromEnable = sio && (gpioOe_[pin / 32u] & mask);
    const bool toOutput = gpioOverride(fromOutput, ctrl >> (pico2 ? 12 : 8));
    const bool toEnable = gpioOverride(fromEnable, ctrl >> (pico2 ? 14 : 12));
    const bool connected = !(reset_ & padsBankResetMask()) && !(pad & 0x100u);
    const bool driven = connected && toEnable && !(pad & 0x80u);
    const bool input = connected && (pad & 0x40u) && (driven ? toOutput : (pad & 8u) && !(pad & 4u));
    const uint32_t status = (uint32_t(toOutput) << 9) | (uint32_t(toEnable) << 13) |
        (uint32_t(input) << 17) | (uint32_t(gpioOverride(input, ctrl >> 28)) << 26);
    return pico2 ? status : status | (uint32_t(fromOutput) << 8) | (uint32_t(fromEnable) << 12) |
        (uint32_t(gpioOverride(input, ctrl >> 16)) << 19) | (uint32_t(input) << 24);
}
bool PeripheralBus::gpio25High() const {
    if (reset_ & (ioBankResetMask() | padsBankResetMask()) || (gpioPad_[25] & 0x180u)) return false;
    const auto status = gpioStatus(25);
    return (status & (1u << 13)) && (status & (1u << 9));
}
bool PeripheralBus::gpioInput(unsigned pin) const {
    return gpioOverride(gpioStatus(pin) & (1u << 17), gpioCtrl_[pin] >> 16);
}
uint32_t PeripheralBus::gpioInputs(unsigned bank) const {
    uint32_t value = 0;
    for (unsigned pin = bank * 32u; pin < gpioCount(chip_) && pin < (bank + 1u) * 32u; ++pin)
        if (gpioInput(pin)) value |= 1u << (pin % 32u);
    return value;
}
bool PeripheralBus::gpioIrqAddress(uint32_t address) const {
    return inRegisters(address, ioBankBase(), gpioIrqOffset(chip_) + 40u * gpioIrqWords(chip_)) &&
        ((address - ioBankBase()) & 0xfffu) >= gpioIrqOffset(chip_);
}
void PeripheralBus::sampleGpioEdges(bool latch) {
    for (unsigned pin = 0; pin < gpioCount(chip_); ++pin) {
        const bool level = gpioStatus(pin) & (1u << 26);
        if (latch && level != gpioIrqLevels_[pin])
            gpioEdges_[pin / 8] |= (level ? 8u : 4u) << (4u * (pin % 8));
        gpioIrqLevels_[pin] = level;
    }
}
uint32_t PeripheralBus::gpioRawIrqs(unsigned word) const {
    if (reset_ & ioBankResetMask()) return 0;
    auto result = gpioEdges_[word];
    for (unsigned pin = 0; pin < 8 && word * 8 + pin < gpioCount(chip_); ++pin)
        result |= (gpioIrqLevels_[word * 8 + pin] ? 2u : 1u) << (pin * 4u);
    return result;
}
uint32_t PeripheralBus::gpioInterrupts(unsigned destination, unsigned word) const {
    const auto enabled = gpioIrqEnable_[destination][word];
    return (enabled ? gpioRawIrqs(word) & enabled : 0u) | gpioIrqForce_[destination][word];
}
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
    gpioCtrl_.fill(gpioCtrlReset);
    gpioPad_.fill(gpioPadReset());
    padVoltage_ = 0;
    reset_ = resetMask();
    watchdogSelect_ = 0;
    gpioEdges_ = {};
    gpioIrqEnable_ = {};
    gpioIrqForce_ = {};
    sampleGpioEdges(false);
}
bool PeripheralBus::handles(uint32_t address) const {
    if (in(address, pwmBase())) return true;
    if (inRegisters(address, ioBankBase(), gpioCount(chip_) * 8u) ||
        inRegisters(address, padsBankBase(), 4u + gpioCount(chip_) * 4u) || gpioIrqAddress(address) ||
        address == 0xd0000004u || (chip_ == Chip::RP2350 && address == 0xd0000008u)) return true;
    if ((address >= 0xd0000010 && address < (chip_ == Chip::RP2040 ? 0xd0000030u : 0xd0000050u)) || in(address, resetBase()) || in(address, watchdogBase())) return true;
    for (unsigned i = 0; i < 2; ++i)
        if (in(address, uartBase(i)) || in(address, spiBase(i)) || ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i)))) return true;
    return false;
}
uint32_t PeripheralBus::read(uint32_t address, bool inspect) {
    if (inRegisters(address, ioBankBase(), gpioCount(chip_) * 8u)) {
        const auto offset = (address - ioBankBase()) & 0xfffu;
        return (offset & 4u) ? gpioCtrl_[offset / 8u] : gpioStatus(offset / 8u);
    }
    if (inRegisters(address, padsBankBase(), 4u + gpioCount(chip_) * 4u)) {
        const auto offset = (address - padsBankBase()) & 0xfffu;
        return offset ? gpioPad_[offset / 4u - 1u] : padVoltage_;
    }
    if (gpioIrqAddress(address)) {
        const unsigned words = gpioIrqWords(chip_);
        const unsigned offset = (((address - ioBankBase()) & 0xfffu) - gpioIrqOffset(chip_)) / 4u;
        const unsigned group = offset / words, word = offset % words;
        if (!group) return gpioRawIrqs(word);
        const unsigned destination = (group - 1) / 3;
        switch ((group - 1) % 3) {
        case 0: return gpioIrqEnable_[destination][word];
        case 1: return gpioIrqForce_[destination][word];
        default: return gpioInterrupts(destination, word);
        }
    }
    if (address == 0xd0000004u || address == 0xd0000008u) return gpioInputs((address - 0xd0000004u) / 4u);
    if (in(address, pwmBase())) return atDevice(pwmBase(), [&] { return pwm_.read(address - pwmBase()); });
    for (unsigned i = 0; i < 2; ++i) {
        if ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i))) return timers_[i].read(address - timerBase(i), inspect);
        if (in(address, uartBase(i))) return atDevice(uartBase(i), [&] { return uarts_[i].read(address - uartBase(i), inspect); });
        if (in(address, spiBase(i))) return atDevice(spiBase(i), [&] { return spis_[i].read(address - spiBase(i), inspect); });
    }
    if (in(address, resetBase())) {
        switch ((address - resetBase()) & 0xfffu) {
        case 0: return reset_; case 4: return watchdogSelect_;
        case 8: return ~reset_ & resetMask();
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
    if (inRegisters(address, ioBankBase(), gpioCount(chip_) * 8u)) {
        const auto offset = address - ioBankBase();
        if (!(offset & 4u)) throw Error(ErrorKind::Unsupported, "GPIO_STATUS en lecture seule", address);
        const auto ctrlMask = chip_ == Chip::RP2040 ? 0x3003331fu : 0x3003f01fu;
        auto& control = gpioCtrl_[(offset & 0xfffu) / 8u];
        if (!(reset_ & ioBankResetMask())) {
            control = atomic(control, value, offset) & ctrlMask;
            sampleGpioEdges(true);
        }
        return;
    }
    if (inRegisters(address, padsBankBase(), 4u + gpioCount(chip_) * 4u)) {
        const auto offset = address - padsBankBase();
        const auto slot = (offset & 0xfffu) / 4u;
        const auto padMask = chip_ == Chip::RP2040 ? 0xffu : 0x1ffu;
        auto& pad = slot ? gpioPad_[slot - 1u] : padVoltage_;
        if (!(reset_ & padsBankResetMask())) {
            pad = atomic(pad, value, offset) & (slot ? padMask : 1u);
            if (slot) sampleGpioEdges(!(reset_ & ioBankResetMask()));
        }
        return;
    }
    if (gpioIrqAddress(address)) {
        const unsigned words = gpioIrqWords(chip_);
        const auto aliasOffset = address - ioBankBase();
        const unsigned offset = ((aliasOffset & 0xfffu) - gpioIrqOffset(chip_)) / 4u;
        const unsigned group = offset / words, word = offset % words;
        if (group && group % 3 == 0) throw Error(ErrorKind::Unsupported, "GPIO_INTS en lecture seule", address);
        if (reset_ & ioBankResetMask()) return;
        if (!group) {
            if (aliasOffset >= 0x1000u) throw Error(ErrorKind::Unsupported, "Utiliser l’adresse de base de GPIO_INTR", address);
            gpioEdges_[word] &= ~value;
        } else {
            const auto mask = chip_ == Chip::RP2040 && word == 3 ? 0x00ffffffu : 0xffffffffu;
            auto& banks = group % 3 == 1 ? gpioIrqEnable_ : gpioIrqForce_;
            auto& entry = banks[(group - 1) / 3][word];
            entry = atomic(entry, value, aliasOffset) & mask;
        }
        return;
    }
    if (in(address, pwmBase())) { if (!held(3, 0)) atDevice(pwmBase(), [&] { pwm_.write(address - pwmBase(), value); }); return; }
    for (unsigned i = 0; i < 2; ++i) {
        if ((i == 0 || chip_ == Chip::RP2350) && in(address, timerBase(i))) { if (!held(0, i)) timers_[i].write(address - timerBase(i), value); return; }
        if (in(address, uartBase(i))) { if (!held(1, i)) atDevice(uartBase(i), [&] { uarts_[i].write(address - uartBase(i), value); }); return; }
        if (in(address, spiBase(i))) { if (!held(2, i)) atDevice(spiBase(i), [&] { spis_[i].write(address - spiBase(i), value); }); return; }
    }
    if (in(address, resetBase())) {
        const auto offset = address - resetBase();
        const auto mask = resetMask();
        if ((offset & 0xfffu) == 4) { watchdogSelect_ = atomic(watchdogSelect_, value, offset) & mask; return; }
        if ((offset & 0xfffu) == 0) {
            const auto old = reset_;
            reset_ = atomic(reset_, value, offset) & mask;
            if (reset_ & ioBankResetMask()) {
                gpioCtrl_.fill(gpioCtrlReset);
                gpioEdges_ = {};
                gpioIrqEnable_ = {};
                gpioIrqForce_ = {};
            }
            if (reset_ & padsBankResetMask()) { gpioPad_.fill(gpioPadReset()); padVoltage_ = 0; }
            if ((old ^ reset_) & (ioBankResetMask() | padsBankResetMask())) sampleGpioEdges(false);
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
        if (!(reset_ & ioBankResetMask())) sampleGpioEdges(true);
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
uint64_t PeripheralBus::interrupts(unsigned core) const {
    uint64_t lines = timers_[0].interrupts();
    if (pwm_.interrupts(0)) lines |= uint64_t(1) << (chip_ == Chip::RP2040 ? 4u : 8u);
    if (chip_ == Chip::RP2350 && pwm_.interrupts(1)) lines |= uint64_t(1) << 9;
    if (chip_ == Chip::RP2350) lines |= uint64_t(timers_[1].interrupts()) << 4;
    for (unsigned word = 0; word < gpioIrqWords(chip_); ++word)
        if (gpioInterrupts(core, word)) { lines |= uint64_t(1) << (chip_ == Chip::RP2350 ? 21u : 13u); break; }
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
