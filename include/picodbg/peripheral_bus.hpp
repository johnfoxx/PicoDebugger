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
    uint64_t interrupts(unsigned core) const;
    bool hasEvents() const;
    bool receiveUart(unsigned index, uint8_t byte);
    const std::string& uartOutput(unsigned index) const { return uarts_.at(index).output(); }
    bool gpio25High() const;
private:
    Chip chip_;
    std::array<Timer, 2> timers_;
    std::array<Uart, 2> uarts_;
    std::array<Spi, 2> spis_;
    Pwm pwm_;
    std::array<uint32_t, 8> scratch_{};
    uint32_t gpioOut_[2]{}, gpioOe_[2]{};
    std::array<uint32_t, 48> gpioCtrl_{}, gpioPad_{};
    std::array<bool, 48> gpioIrqLevels_{};
    std::array<uint32_t, 6> gpioEdges_{};
    std::array<std::array<uint32_t, 6>, 3> gpioIrqEnable_{}, gpioIrqForce_{};
    uint32_t padVoltage_ = 0;
    uint32_t reset_ = 0, watchdogSelect_ = 0;
    uint32_t timerBase(unsigned index) const;
    uint32_t uartBase(unsigned index) const;
    uint32_t spiBase(unsigned index) const;
    uint32_t resetBase() const;
    uint32_t resetMask() const;
    uint32_t watchdogBase() const;
    uint32_t ioBankBase() const;
    uint32_t padsBankBase() const;
    uint32_t ioBankResetMask() const;
    uint32_t padsBankResetMask() const;
    uint32_t gpioPadReset() const;
    uint32_t gpioStatus(unsigned pin) const;
    bool gpioInput(unsigned pin) const;
    uint32_t gpioInputs(unsigned bank) const;
    bool gpioIrqAddress(uint32_t address) const;
    void sampleGpioEdges(bool latch);
    uint32_t gpioRawIrqs(unsigned word) const;
    uint32_t gpioInterrupts(unsigned destination, unsigned word) const;
    uint32_t pwmBase() const { return chip_ == Chip::RP2040 ? 0x40050000u : 0x400a8000u; }
    unsigned resetBit(unsigned kind, unsigned index) const;
    bool held(unsigned kind, unsigned index) const;
};
}
