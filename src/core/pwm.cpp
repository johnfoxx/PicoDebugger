#include "picodbg/pwm.hpp"

namespace picodbg {
namespace {
void unsupported(uint32_t offset) { throw Error(ErrorKind::Unsupported, "PWM : registre ou mode hors modèle", offset); }
}
void Pwm::reset() { slices_ = {}; raw_ = 0; masks_.fill(0); forced_.fill(0); }
void Pwm::latch(Slice& slice) { slice.liveTop = slice.top; slice.liveCompare = slice.compare; }
uint32_t Pwm::read(uint32_t offset) const {
    if (offset >= 0x4000 || (offset & 3u)) unsupported(offset);
    const auto reg = offset & 0xfffu, common = count_ * 0x14u;
    if (reg < common) {
        const auto& s = slices_[reg / 0x14u];
        switch (reg % 0x14u) { case 0: return s.csr; case 4: return s.div; case 8: return s.counter; case 12: return s.compare; case 16: return s.top; }
    }
    if (reg == common) { uint32_t enabled = 0; for (unsigned i = 0; i < count_; ++i) enabled |= (slices_[i].csr & 1u) << i; return enabled; }
    if (reg == common + 4) return raw_;
    if (reg >= common + 8 && reg < common + (count_ == 12 ? 32u : 20u)) {
        const auto irq = (reg - common - 8) / 12;
        switch ((reg - common - 8) % 12) { case 0: return masks_[irq]; case 4: return forced_[irq]; case 8: return interrupts(irq); }
    }
    unsupported(offset); return 0;
}
void Pwm::write(uint32_t offset, uint32_t value) {
    if (offset >= 0x4000 || (offset & 3u)) unsupported(offset);
    const auto reg = offset & 0xfffu, common = count_ * 0x14u, alias = offset >> 12;
    if (alias) {
        if (reg == common + 4 || (reg >= common + 8 && (reg - common - 8) % 12 == 8)) unsupported(offset);
        const auto old = read(reg); value = alias == 1 ? old ^ value : alias == 2 ? old | value : old & ~value;
    }
    if (reg < common) {
        auto& s = slices_[reg / 0x14u];
        switch (reg % 0x14u) {
        case 0:
            if (value & 0xf0u) unsupported(offset); // External B gating and phase-adjust pulses need pin/clock models.
            if (!(s.csr & 1u)) latch(s);
            s.csr = value & 15u; if (!(s.csr & 1u)) latch(s); return;
        case 4: if ((value & 0xfffu) < 16) unsupported(offset); s.div = value & 0xfffu; s.fraction = 0; return;
        case 8: s.counter = value & 0xffffu; return;
        case 12: s.compare = value; if (!(s.csr & 1u)) latch(s); return;
        case 16: s.top = value & 0xffffu; if (!(s.csr & 1u)) latch(s); return;
        }
    }
    const auto mask = (1u << count_) - 1u;
    if (reg == common) {
        for (unsigned i = 0; i < count_; ++i) { auto& s = slices_[i]; if (!(s.csr & 1u)) latch(s); s.csr = (s.csr & ~1u) | ((value >> i) & 1u); if (!(s.csr & 1u)) latch(s); }
        return;
    }
    if (reg == common + 4) { raw_ &= ~(value & mask); return; }
    if (reg >= common + 8 && reg < common + (count_ == 12 ? 32u : 20u)) {
        const auto irq = (reg - common - 8) / 12;
        switch ((reg - common - 8) % 12) { case 0: masks_[irq] = value & mask; return; case 4: forced_[irq] = value & mask; return; }
    }
    unsupported(offset);
}
bool Pwm::running() const { for (unsigned i = 0; i < count_; ++i) if (slices_[i].csr & 1u) return true; return false; }
void Pwm::tick() {
    for (unsigned i = 0; i < count_; ++i) {
        auto& s = slices_[i]; if (!(s.csr & 1u)) continue;
        s.fraction += 16; if (s.fraction < s.div) continue; s.fraction -= s.div;
        bool wrap = false;
        if (s.csr & 2u) {
            if (!s.liveTop) { s.counter = 0; wrap = true; }
            else if (s.down) { if (s.counter) --s.counter; if (!s.counter) { s.down = false; wrap = true; } }
            else { s.counter = (s.counter + 1u) & 0xffffu; if (s.counter == s.liveTop) s.down = true; }
        } else if (s.counter == s.liveTop) { s.counter = 0; wrap = true; }
        else s.counter = (s.counter + 1u) & 0xffffu;
        // TOP and CC are double buffered while running. Commit only at the
        // wrap boundary; reading their registers still returns the staged value.
        if (wrap) { raw_ |= 1u << i; latch(s); }
    }
}
}
