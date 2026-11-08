#pragma once
#include "picodbg/thumb.hpp"
#include <array>

namespace picodbg {
struct Cpu {
    std::array<uint32_t, 16> r;
    uint32_t xpsr = 0x01000000, msp = 0, psp = 0, control = 0, primask = 0;
    bool held = true, sleeping = false, sleepOnEvent = false, event = false, lockup = false;
    uint64_t instructions = 0;
    uint32_t faultAddress = 0;
    std::string lastFault;
    Cpu() { r.fill(0); }
    bool handlerMode() const { return (xpsr & 0x1ffu) != 0; }
    void reset(uint32_t pc, uint32_t sp);
    void syncSp();
    bool condition(unsigned code) const;
    std::string flags() const;
};
struct StepResult {
    bool breakpoint = false;
    bool exception = false;
    bool sendEvent = false;
    std::string detail;
};
StepResult execute(Cpu& cpu, Memory& memory, unsigned core);
bool enterException(Cpu& cpu, Memory& memory, unsigned core, unsigned number, uint32_t returnPc);
}
