#include "picodbg/timer.hpp"

namespace picodbg {
namespace {
const uint32_t base = 0x40054000;
void unsupported(uint32_t offset) {
    throw Error(ErrorKind::Unsupported, "Registre/alias TIMER RP2040 non pris en charge", base + offset);
}
}
void Timer::reset() {
    time_ = 0; writeLow_ = readHigh_ = 0; alarms_.fill(0);
    armed_ = rawInterrupts_ = enabled_ = forced_ = 0;
    debugPause_ = 7; paused_ = false;
}
uint32_t Timer::read(uint32_t offset, bool inspect) {
    // Atomic aliases change writes only. Reads use the same register address.
    const uint32_t reg = offset & 0xfffu;
    if (offset >= 0x4000 || (offset & 3u)) unsupported(offset);
    if (reg >= 0x10 && reg <= 0x1c) return alarms_[(reg - 0x10) / 4];
    switch (reg) {
    case 0x08: return readHigh_;
    case 0x0c:
        if (!inspect) readHigh_ = static_cast<uint32_t>(time_ >> 32);
        return static_cast<uint32_t>(time_);
    case 0x20: return armed_;
    case 0x24: return static_cast<uint32_t>(time_ >> 32);
    case 0x28: return static_cast<uint32_t>(time_);
    case 0x2c: return debugPause_;
    case 0x30: return paused_ ? 1u : 0u;
    case 0x34: return rawInterrupts_;
    case 0x38: return enabled_;
    case 0x3c: return forced_;
    case 0x40: return interrupts();
    default: unsupported(offset); return 0;
    }
}
void Timer::fire(unsigned index) {
    armed_ &= ~(1u << index);
    rawInterrupts_ |= 1u << index;
}
void Timer::write(uint32_t offset, uint32_t value) {
    if (offset >= 0x4000 || (offset & 3u)) unsupported(offset);
    const uint32_t reg = offset & 0xfffu;
    const unsigned alias = offset >> 12;
    if (alias) {
        // Only ordinary RW registers have validated atomic semantics here.
        // Reject W1C/FIFO aliases instead of incorrectly doing read-modify-write.
        if (!((reg >= 0x10 && reg <= 0x1c) || reg == 0x2c || reg == 0x30 || reg == 0x38 || reg == 0x3c)) unsupported(offset);
        const uint32_t old = read(reg, true);
        value = alias == 1 ? old ^ value : alias == 2 ? old | value : old & ~value;
    }
    if (reg >= 0x10 && reg <= 0x1c) {
        const unsigned index = (reg - 0x10) / 4;
        alarms_[index] = value; armed_ |= 1u << index;
        if (static_cast<uint32_t>(time_) == value) fire(index);
        return;
    }
    switch (reg) {
    case 0x00:
        time_ = (uint64_t(value) << 32) | writeLow_;
        // Reprogramming time jumps the comparator to the new value; it does
        // not traverse the skipped interval as advance() does.
        for (unsigned index = 0; index < 4; ++index)
            if ((armed_ & (1u << index)) && alarms_[index] == writeLow_) fire(index);
        return;
    case 0x04: writeLow_ = value; return;
    case 0x20: armed_ &= ~(value & 15u); return;
    case 0x2c: debugPause_ = (value & 6u) | (debugPause_ & 1u); return;
    case 0x30: paused_ = value & 1u; return;
    case 0x34: rawInterrupts_ &= ~(value & 15u); return;
    case 0x38: enabled_ = value & 15u; return;
    case 0x3c: forced_ = value & 15u; return;
    default: unsupported(offset);
    }
}
void Timer::advance(uint64_t ticks, unsigned debugHaltedCores) {
    if (!ticks || paused_ || (debugPause_ & ((debugHaltedCores & 3u) << 1))) return;
    const uint32_t low = static_cast<uint32_t>(time_);
    for (unsigned index = 0; index < 4; ++index) {
        if (!(armed_ & (1u << index))) continue;
        // Compare over the complete interval, including low-word wrap. A bulk
        // advance cannot skip an alarm, even when it crosses multiple wraps.
        uint64_t distance = uint32_t(alarms_[index] - low);
        if (!distance) distance = 0x100000000ull;
        if (ticks >= distance) fire(index);
    }
    time_ += ticks;
}
}
