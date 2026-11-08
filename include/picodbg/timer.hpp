#pragma once
#include "picodbg/types.hpp"
#include <array>

namespace picodbg {
// Functional RP2040 timer, independent of host time and CPU instruction speed.
// The caller supplies virtual microsecond ticks; no wall-clock approximation.
class Timer {
public:
    Timer() { reset(); }
    void reset();
    uint32_t read(uint32_t offset, bool inspect);
    void write(uint32_t offset, uint32_t value);
    void advance(uint64_t ticks, unsigned debugHaltedCores = 0);
    uint32_t interrupts() const { return (rawInterrupts_ & enabled_) | forced_; }
    bool hasScheduledAlarm() const { return armed_ != 0 && !paused_; }
private:
    uint64_t time_ = 0;
    uint32_t writeLow_ = 0, readHigh_ = 0;
    std::array<uint32_t, 4> alarms_;
    uint32_t armed_ = 0, rawInterrupts_ = 0, enabled_ = 0, forced_ = 0;
    uint32_t debugPause_ = 7;
    bool paused_ = false;
    void fire(unsigned index);
};
}
