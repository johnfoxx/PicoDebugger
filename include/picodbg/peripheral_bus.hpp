#pragma once
#include "picodbg/serial.hpp"
#include "picodbg/timer.hpp"
#include "picodbg/pwm.hpp"
#include <array>

namespace picodbg {
// Shared device state and address routing, independent of CPU/Qt. Unknown
// registers remain Unsupported even inside a device's decoded aperture.
class PeripheralBus {
public:
    explicit PeripheralBus(Chip chip);
    void reset();
    bool handles(uint32_t address) const;
    uint32_t read(uint32_t address, bool inspect);
    void write(uint32_t address, uint32_t value);
    void advance(uint64_t quanta);
    uint64_t interrupts() const;
    bool hasEvents() const;
    bool receiveUart(unsigned index, uint8_t byte);
    const std::string& uartOutput(unsigned index) const { return uarts_.at(index).output(); }
private:
    Chip chip_;
    std::array<Timer, 2> timers_;
    std::array<Uart, 2> uarts_;
    std::array<Spi, 2> spis_;
    Pwm pwm_;
    std::array<uint32_t, 8> scratch_{};
    uint32_t gpioOut_[2]{}, gpioOe_[2]{};
    uint32_t reset_ = 0, watchdogSelect_ = 0;
    uint32_t timerBase(unsigned index) const;
    uint32_t uartBase(unsigned index) const;
    uint32_t spiBase(unsigned index) const;
    uint32_t resetBase() const;
    uint32_t watchdogBase() const;
    uint32_t pwmBase() const { return chip_ == Chip::RP2040 ? 0x40050000u : 0x400a8000u; }
    unsigned resetBit(unsigned kind, unsigned index) const;
    bool held(unsigned kind, unsigned index) const;
};
}
