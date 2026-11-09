#pragma once
#include "picodbg/types.hpp"
#include <deque>
#include <string>

namespace picodbg {
// Functional PL011 / PL022 models: one transfer per virtual quantum, no baud
// timing or pin multiplexing. FIFOs and inspection are deliberately separate.
class Uart {
public:
    Uart() { reset(); }
    void reset();
    uint32_t read(uint32_t offset, bool inspect);
    void write(uint32_t offset, uint32_t value);
    void tick();
    bool receive(uint8_t byte);
    uint32_t interrupts() const;
    bool hasWork() const;
    const std::string& output() const { return output_; }
private:
    std::deque<uint8_t> rx_, tx_;
    std::string output_;
    uint32_t integer_ = 0, fraction_ = 0, line_ = 0, control_ = 0x300, levels_ = 0x12, mask_ = 0;
    uint32_t errors_ = 0, sticky_ = 0;
    unsigned idleTicks_ = 0;
    size_t capacity() const { return line_ & 16u ? 32u : 1u; }
    uint32_t rawInterrupts() const;
    unsigned threshold(bool receive) const;
};

class Spi {
public:
    Spi() { reset(); }
    void reset();
    uint32_t read(uint32_t offset, bool inspect);
    void write(uint32_t offset, uint32_t value);
    void tick();
    uint32_t interrupts() const;
    bool hasWork() const { return ((control1_ & 2u) && !tx_.empty()) || (!rx_.empty() && idleTicks_ < 32); }
private:
    std::deque<uint16_t> rx_, tx_;
    uint32_t control0_ = 0, control1_ = 0, prescale_ = 0, mask_ = 0, sticky_ = 0;
    unsigned idleTicks_ = 0;
    uint32_t rawInterrupts() const;
};
}
