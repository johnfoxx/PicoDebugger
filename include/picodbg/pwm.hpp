#pragma once
#include "picodbg/chip.hpp"
#include <array>

namespace picodbg {
class Pwm {
public:
    explicit Pwm(Chip chip) : count_(chip == Chip::RP2350 ? 12u : 8u) { reset(); }
    void reset();
    uint32_t read(uint32_t offset) const;
    void write(uint32_t offset, uint32_t value);
    void tick();
    bool running() const;
    uint32_t interrupts(unsigned line) const { return (raw_ & masks_.at(line)) | forced_.at(line); }
private:
    struct Slice {
        uint32_t csr = 0, div = 16, counter = 0, compare = 0, top = 0xffff;
        uint32_t liveTop = 0xffff, liveCompare = 0, fraction = 0;
        bool down = false;
    };
    unsigned count_;
    std::array<Slice, 12> slices_;
    uint32_t raw_ = 0;
    std::array<uint32_t, 2> masks_{}, forced_{};
    void latch(Slice& slice);
};
}
