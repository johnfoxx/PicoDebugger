#include "picodbg/serial.hpp"

namespace picodbg {
namespace {
void unsupported(const char* device, uint32_t offset) {
    throw Error(ErrorKind::Unsupported, std::string(device) + " : registre ou mode hors modèle", offset);
}
uint32_t atomic(uint32_t old, uint32_t value, uint32_t offset) {
    switch (offset >> 12) { case 0: return value; case 1: return old ^ value; case 2: return old | value; default: return old & ~value; }
}
void checkOffset(uint32_t offset) {
    if (offset >= 0x4000 || (offset & 3u)) unsupported("Port série", offset);
}
}
void Uart::reset() {
    rx_.clear(); tx_.clear(); output_.clear();
    integer_ = fraction_ = line_ = mask_ = errors_ = sticky_ = idleTicks_ = 0;
    control_ = 0x300; levels_ = 0x12;
}
unsigned Uart::threshold(bool receive) const {
    static const unsigned levels[] = {4, 8, 16, 24, 28};
    if (!(line_ & 16u)) return receive ? 1u : 0u;
    return levels[(levels_ >> (receive ? 3u : 0u)) & 7u];
}
uint32_t Uart::rawInterrupts() const { return sticky_; }
uint32_t Uart::interrupts() const { return rawInterrupts() & mask_; }
uint32_t Uart::read(uint32_t offset, bool inspect) {
    checkOffset(offset);
    switch (offset & 0xfffu) {
    case 0: {
        const uint32_t value = rx_.empty() ? 0u : rx_.front();
        if (!inspect && !rx_.empty()) {
            rx_.pop_front(); idleTicks_ = 0; sticky_ &= ~0x40u;
            if (rx_.size() < threshold(true)) sticky_ &= ~0x10u;
        }
        return value | (errors_ << 8);
    }
    case 4: return errors_;
    case 0x18: return (rx_.empty() ? 0x10u : 0u) | (tx_.empty() ? 0x80u : 8u) |
        (rx_.size() >= capacity() ? 0x40u : 0u) | (tx_.size() >= capacity() ? 0x20u : 0u);
    case 0x24: return integer_; case 0x28: return fraction_; case 0x2c: return line_;
    case 0x30: return control_; case 0x34: return levels_; case 0x38: return mask_;
    case 0x3c: return rawInterrupts(); case 0x40: return interrupts(); case 0x48: return 0;
    default: unsupported("UART", offset); return 0;
    }
}
void Uart::write(uint32_t offset, uint32_t value) {
    checkOffset(offset); const auto reg = offset & 0xfffu;
    if (offset >> 12) {
        if (reg < 0x24 || reg > 0x38) unsupported("UART alias", offset);
        value = atomic(read(reg, true), value, offset);
    }
    switch (reg) {
    case 0:
        if (tx_.size() >= capacity()) throw Error(ErrorKind::Unsupported, "UART TX pleine : attente du bus non émulée", offset);
        tx_.push_back(static_cast<uint8_t>(value));
        if (tx_.size() > threshold(false)) sticky_ &= ~0x20u;
        return;
    case 4: errors_ = 0; return;
    case 0x24: integer_ = value & 0xffffu; return;
    case 0x28: fraction_ = value & 63u; return;
    case 0x2c:
        if (value & 1u) unsupported("UART break", offset);
        if ((line_ ^ value) & 16u) { rx_.clear(); tx_.clear(); sticky_ &= ~0x70u; }
        line_ = value & 0xfeu; return;
    case 0x30:
        if (value & 0xc006u) unsupported("UART IrDA/contrôle de flux", offset);
        control_ = value & 0xff81u; return;
    case 0x34:
        if ((value & 7u) > 4 || ((value >> 3) & 7u) > 4) unsupported("UART seuil réservé", offset);
        levels_ = value & 63u; return;
    case 0x38: mask_ = value & 0x7ffu; return;
    case 0x44: sticky_ &= ~(value & 0x7ffu); return;
    case 0x48: if (value & 7u) unsupported("UART DMA", offset); return;
    default: unsupported("UART", offset);
    }
}
bool Uart::receive(uint8_t byte) {
    if ((control_ & 0x201u) != 0x201u) return false;
    if (rx_.size() >= capacity()) { errors_ |= 8u; sticky_ |= 0x400u; return false; }
    const uint8_t dataMask = static_cast<uint8_t>((1u << (5u + ((line_ >> 5) & 3u))) - 1u);
    rx_.push_back(byte & dataMask); idleTicks_ = 0; sticky_ &= ~0x40u;
    if (rx_.size() == threshold(true)) sticky_ |= 0x10u;
    return true;
}
bool Uart::hasWork() const { return ((control_ & 0x101u) == 0x101u && !tx_.empty()) || (!rx_.empty() && idleTicks_ < 32); }
void Uart::tick() {
    if ((control_ & 0x101u) == 0x101u && !tx_.empty()) {
        const auto previousSize = tx_.size();
        const uint8_t byte = static_cast<uint8_t>(tx_.front() & ((1u << (5u + ((line_ >> 5) & 3u))) - 1u)); tx_.pop_front();
        // PL011 interrupts latch on threshold crossings. An initially empty
        // FIFO must not spuriously interrupt when the UART is first enabled.
        if (previousSize > threshold(false) && tx_.size() <= threshold(false)) sticky_ |= 0x20u;
        if (control_ & 0x80u) receive(byte);
        else {
            if (output_.size() >= 65536) output_.erase(0, 4096);
            output_.push_back(static_cast<char>(byte));
        }
    }
    // Timeout is in virtual quanta, not UART bit periods; no baud claim.
    if (!rx_.empty() && idleTicks_ < 32 && ++idleTicks_ == 32) sticky_ |= 0x40u;
}
void Spi::reset() { rx_.clear(); tx_.clear(); control0_ = control1_ = prescale_ = mask_ = sticky_ = idleTicks_ = 0; }
uint32_t Spi::rawInterrupts() const { return sticky_ | (rx_.size() >= 4 ? 4u : 0u) | (tx_.size() <= 4 ? 8u : 0u); }
uint32_t Spi::interrupts() const { return rawInterrupts() & mask_; }
uint32_t Spi::read(uint32_t offset, bool inspect) {
    checkOffset(offset);
    switch (offset & 0xfffu) {
    case 0: return control0_; case 4: return control1_;
    case 8: {
        const uint32_t value = rx_.empty() ? 0u : rx_.front();
        if (!inspect && !rx_.empty()) { rx_.pop_front(); idleTicks_ = 0; sticky_ &= ~2u; }
        return value;
    }
    case 0xc: return (tx_.empty() ? 1u : 16u) | (tx_.size() < 8 ? 2u : 0u) | (!rx_.empty() ? 4u : 0u) | (rx_.size() == 8 ? 8u : 0u);
    case 0x10: return prescale_; case 0x14: return mask_; case 0x18: return rawInterrupts(); case 0x1c: return interrupts(); case 0x24: return 0;
    default: unsupported("SPI", offset); return 0;
    }
}
void Spi::write(uint32_t offset, uint32_t value) {
    checkOffset(offset); const auto reg = offset & 0xfffu;
    if (offset >> 12) {
        if (reg != 0 && reg != 4 && reg != 0x10 && reg != 0x14) unsupported("SPI alias", offset);
        value = atomic(read(reg, true), value, offset);
    }
    switch (reg) {
    case 0:
        if ((value & 15u) < 3 || (value & 0x30u)) unsupported("SPI format hors modèle", offset);
        control0_ = value & 0xffffu; return;
    case 4: if (value & 4u) unsupported("SPI esclave", offset); control1_ = value & 11u; return;
    case 8:
        if (tx_.size() == 8) unsupported("SPI TX pleine", offset);
        tx_.push_back(static_cast<uint16_t>(value)); return;
    case 0x10: if ((value & 1u) || (value & 255u) < 2) unsupported("SPI préscalaire invalide", offset); prescale_ = value & 255u; return;
    case 0x14: mask_ = value & 15u; return;
    case 0x20: sticky_ &= ~(value & 3u); return;
    case 0x24: if (value & 3u) unsupported("SPI DMA", offset); return;
    default: unsupported("SPI", offset);
    }
}
void Spi::tick() {
    if ((control1_ & 2u) && !tx_.empty()) {
        // No external SPI device is invented. Only the real internal loopback
        // mode is executable until an explicit board/device model is attached.
        if (!(control1_ & 1u)) unsupported("SPI sans périphérique externe (activer LBM pour loopback)", 4);
        const uint32_t mask = (1u << ((control0_ & 15u) + 1u)) - 1u;
        const auto value = static_cast<uint16_t>(tx_.front() & mask); tx_.pop_front();
        if (rx_.size() == 8) sticky_ |= 1u; else rx_.push_back(value);
        idleTicks_ = 0;
    }
    if (!rx_.empty() && idleTicks_ < 32 && ++idleTicks_ == 32) sticky_ |= 2u;
}
}
