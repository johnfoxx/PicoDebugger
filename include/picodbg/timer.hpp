#pragma once
#include "picodbg/types.hpp"
#include "picodbg/chip.hpp"
#include <array>

namespace picodbg {
// Functional RP2040/RP2350 timer, independent of host time and instruction speed.
// The caller supplies virtual microsecond ticks; no wall-clock approximation.
class Timer {
public:
    explicit Timer(Chip chip = Chip::RP2040, uint32_t base = 0x40054000) : chip_(chip), base_(base) { reset(); }
    void reset();
    uint32_t read(uint32_t offset, bool inspect);
    void write(uint32_t offset, uint32_t value);
    void advance(uint64_t ticks, unsigned debugHaltedCores = 0);
    void advanceClocks(uint64_t ticks, uint64_t systemCycles);
    uint32_t interrupts() const { return (rawInterrupts_ & enabled_) | forced_; }
    bool hasScheduledAlarm() const { return armed_ != 0 && !paused_; }
private:
    Chip chip_;
    uint32_t base_;
    bool locked_ = false, systemSource_ = false;
    uint32_t canonical(uint32_t offset) const;
    void unsupported(uint32_t offset) const;
    uint64_t time_ = 0;
    uint32_t writeLow_ = 0, readHigh_ = 0;
    std::array<uint32_t, 4> alarms_;
    uint32_t armed_ = 0, rawInterrupts_ = 0, enabled_ = 0, forced_ = 0;
    uint32_t debugPause_ = 7;
    bool paused_ = false;
    void fire(unsigned index);
};
}
