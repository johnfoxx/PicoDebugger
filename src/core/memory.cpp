#include "picodbg/memory.hpp"
#include <algorithm>
#include <limits>

namespace picodbg {
namespace {
bool inside(uint32_t address, unsigned size, uint32_t base, size_t length) {
    return address >= base && uint64_t(address) + size <= uint64_t(base) + length;
}
uint32_t unpack(const std::vector<uint8_t>& data, size_t at, unsigned size) {
    uint32_t result = 0;
    for (unsigned i = 0; i < size; ++i) result |= uint32_t(data.at(at + i)) << (i * 8);
    return result;
}
}
Memory::Memory(Chip chip) : description_(describe(chip)), ram_(description_.ramSize), flash_(description_.flashSize, 0xff), peripherals_(chip) { clear(); }
void Memory::clear() {
    std::fill(ram_.begin(), ram_.end(), 0);
    std::fill(flash_.begin(), flash_.end(), 0xff);
    system_ = {}; locks_.fill(false); fifoErrors_.fill(0);
    fifo_[0].clear(); fifo_[1].clear();
    peripherals_.reset();
}
void Memory::check(uint32_t address, unsigned size, unsigned core) const {
    if (core > 1 || (size != 1 && size != 2 && size != 4))
        throw Error(ErrorKind::InvalidArgument, "Taille d'accès ou cœur invalide", address);
    if (uint64_t(address) + size > 0x100000000ull)
        throw Error(ErrorKind::BusFault, "Débordement d'adresse", address);
}
uint32_t Memory::ramOffset(uint32_t address) const {
    if (inside(address, 1, 0x20000000, ram_.size())) return address - 0x20000000;
    if (description_.chip == Chip::RP2040 && inside(address, 1, 0x21000000, 0x40000)) {
        // Non-striped aliases select a bank; the linear view stripes 32-bit words
        // over four banks. A simple subtraction here would alias the wrong data.
        const uint32_t offset = address - 0x21000000;
        return (offset & 0xffffu & ~3u) * 4u + (offset >> 16) * 4u + (offset & 3u);
    }
    return std::numeric_limits<uint32_t>::max();
}
uint32_t Memory::read(uint32_t address, unsigned size, unsigned core, Access access) {
    check(address, size, core);
    if (description_.chip == Chip::RP2040 && address % size)
        throw Error(ErrorKind::Alignment, "Accès non aligné Cortex-M0+", address);
    const auto value = readImpl(address, size, core, access, false);
    if (onAccess) onAccess({address, size, access, core});
    return value;
}
uint32_t Memory::peek(uint32_t address, unsigned size, unsigned core) const {
    check(address, size, core);
    // The inspect flag excludes every device mutation in readImpl/readDevice.
    return const_cast<Memory*>(this)->readImpl(address, size, core, Access::Read, true);
}
uint32_t Memory::readImpl(uint32_t address, unsigned size, unsigned core, Access access, bool inspect) {
    if (inside(address, size, 0x10000000, flash_.size())) return unpack(flash_, address - 0x10000000, size);
    if (ramOffset(address) != std::numeric_limits<uint32_t>::max()) {
        uint32_t value = 0;
        for (unsigned i = 0; i < size; ++i) {
            const uint32_t offset = ramOffset(address + i);
            if (offset >= ram_.size()) throw Error(ErrorKind::BusFault, "Lecture hors SRAM", address);
            value |= uint32_t(ram_[offset]) << (8u * i);
        }
        return value;
    }
    if (access == Access::Execute) {
        const auto* region = regionAt(description_, address);
        throw Error(region ? ErrorKind::Unsupported : ErrorKind::BusFault, "Exécution hors mémoire modélisée à " + hex(address), address);
    }
    if (size != 4 || address % 4) {
        throw Error(ErrorKind::Unsupported, "Accès périphérique hors modèle (seuls les mots alignés sont pris en charge)", address);
    }
    return readDevice(address, core, inspect);
}
void Memory::write(uint32_t address, uint32_t value, unsigned size, unsigned core) {
    poke(address, value, size, core);
    if (onAccess) onAccess({address, size, Access::Write, core});
}
void Memory::poke(uint32_t address, uint32_t value, unsigned size, unsigned core) {
    check(address, size, core);
    if (description_.chip == Chip::RP2040 && address % size)
        throw Error(ErrorKind::Alignment, "Écriture non alignée Cortex-M0+", address);
    if (ramOffset(address) != std::numeric_limits<uint32_t>::max()) {
        // Validate the whole access first: an out-of-range store is never partial.
        for (unsigned i = 0; i < size; ++i)
            if (ramOffset(address + i) >= ram_.size()) throw Error(ErrorKind::BusFault, "Écriture hors SRAM", address);
        for (unsigned i = 0; i < size; ++i) ram_[ramOffset(address + i)] = uint8_t(value >> (8u * i));
    } else if (inside(address, size, 0x10000000, flash_.size())) {
        throw Error(ErrorKind::Unsupported, "Écriture XIP : contrôleur flash non émulé", address);
    } else {
        if (size != 4 || address % 4) throw Error(ErrorKind::Unsupported, "Écriture MMIO hors modèle", address);
        writeDevice(address, value, core);
    }
}
void Memory::load(uint32_t address, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return;
    if (uint64_t(address) + bytes.size() > 0x100000000ull) throw Error(ErrorKind::InvalidImage, "Segment ELF débordant", address);
    if (inside(address, 0, 0x10000000, flash_.size()) && uint64_t(address) + bytes.size() <= 0x10000000ull + flash_.size())
        std::copy(bytes.begin(), bytes.end(), flash_.begin() + (address - 0x10000000));
    else if (inside(address, 0, 0x20000000, ram_.size()) && uint64_t(address) + bytes.size() <= 0x20000000ull + ram_.size())
        std::copy(bytes.begin(), bytes.end(), ram_.begin() + (address - 0x20000000));
    else throw Error(ErrorKind::InvalidImage, "Segment ELF hors Flash/SRAM du profil : " + hex(address), address);
}
uint32_t Memory::readDevice(uint32_t address, unsigned core, bool inspect) {
    if (peripherals_.handles(address)) return peripherals_.read(address, inspect);
    auto& s = system_[core];
    if (description_.chip == Chip::RP2350) {
        switch (address) {
        case 0xe000e104: case 0xe000e184: return static_cast<uint32_t>(s.enabled >> 32);
        case 0xe000e204: case 0xe000e284: return static_cast<uint32_t>(s.pending >> 32);
        case 0xe000e300: return static_cast<uint32_t>(s.active);
        case 0xe000e304: return static_cast<uint32_t>(s.active >> 32);
        }
    }
    switch (address) {
    case 0xd0000000: return core;
    case 0xd0000050: return (fifo_[core].empty() ? 0u : 1u) | (fifo_[1-core].size() < 8 ? 2u : 0u) | fifoErrors_[core];
    case 0xd0000058: {
        if (fifo_[core].empty()) { if (!inspect) fifoErrors_[core] |= 8u; return 0; }
        const auto value = fifo_[core].front();
        if (!inspect) fifo_[core].pop_front();
        return value;
    }
    default: break;
    }
    if (address >= 0xd0000100 && address < 0xd0000180) {
        const unsigned index = (address - 0xd0000100) / 4;
        const auto available = !locks_[index];
        if (!inspect) locks_[index] = true;
        return available ? (uint32_t(1) << index) : 0;
    }
    {
        switch (address) {
        case 0xe000e010: {
            const auto result = s.systickCtrl | (s.systickCountFlag ? 0x10000u : 0u);
            if (!inspect) s.systickCountFlag = false;
            return result;
        }
        case 0xe000e014: return s.systickLoad;
        case 0xe000e018: return s.systickValue;
        case 0xe000e100: case 0xe000e180: return static_cast<uint32_t>(s.enabled);
        case 0xe000e200: case 0xe000e280: return static_cast<uint32_t>(s.pending);
        case 0xe000ed08: return s.vtor;
        default: break;
        }
    }
    const auto* region = regionAt(description_, address);
    throw Error(region ? ErrorKind::Unsupported : ErrorKind::BusFault,
        std::string(region ? "Registre non émulé : " + region->name : "Adresse non cartographiée") + " à " + hex(address), address);
}
void Memory::writeDevice(uint32_t address, uint32_t value, unsigned core) {
    if (peripherals_.handles(address)) { peripherals_.write(address, value); updateInterrupts(); return; }
    auto& s = system_[core];
    if (description_.chip == Chip::RP2350) {
        const uint64_t high = uint64_t(value & 0xfffffu) << 32;
        switch (address) {
        case 0xe000e104: s.enabled |= high; return;
        case 0xe000e184: s.enabled &= ~high; return;
        case 0xe000e204: s.pending |= high; return;
        case 0xe000e284: s.pending &= ~high; return;
        }
    }
    if (address == 0xd0000050) { fifoErrors_[core] &= ~(value & 12u); return; }
    if (address == 0xd0000054) {
        if (fifo_[1-core].size() == 8) fifoErrors_[core] |= 4u;
        else fifo_[1-core].push_back(value);
        return;
    }
    if (address >= 0xd0000100 && address < 0xd0000180) { locks_[(address - 0xd0000100) / 4] = false; return; }
    {
        switch (address) {
        case 0xe000e010:
            if ((value & 1u) && !(value & 4u)) throw Error(ErrorKind::Unsupported, "Horloge SysTick externe non émulée", address);
            s.systickCtrl = value & 7u; return;
        case 0xe000e014: s.systickLoad = value & 0xffffffu; return;
        case 0xe000e018: s.systickValue = 0; s.systickCountFlag = false; return;
        case 0xe000e100: s.enabled |= value; return;
        case 0xe000e180: s.enabled &= ~uint64_t(value); return;
        case 0xe000e200: s.pending |= value; return;
        case 0xe000e280: s.pending &= ~uint64_t(value); return;
        case 0xe000ed08: s.vtor = value & (description_.chip == Chip::RP2350 ? 0xffffff80u : 0xffffff00u); return;
        default: break;
        }
    }
    const auto* region = regionAt(description_, address);
    throw Error(region ? ErrorKind::Unsupported : ErrorKind::BusFault, "Écriture non prise en charge à " + hex(address) + (region ? " (" + region->name + ")" : ""), address);
}
void Memory::tick(unsigned core) {
    auto& s = system_.at(core);
    if (!fifo_[core].empty() || fifoErrors_[core])
        s.pending |= (uint64_t(1) << (description_.chip == Chip::RP2040 ? 15u + core : 25u)) & ~s.active;
    s.pending |= peripherals_.interrupts() & ~s.active;
    if (!(s.systickCtrl & 1u)) return;
    if (s.systickValue == 0) s.systickValue = s.systickLoad;
    else if (--s.systickValue == 0) {
        s.systickCountFlag = true;
        if (s.systickCtrl & 2u) s.systickPending = true;
    }
}
void Memory::advanceTime(uint64_t virtualMicroseconds) {
    peripherals_.advance(virtualMicroseconds); updateInterrupts();
}
void Memory::updateInterrupts() {
    for (auto& s : system_) s.pending |= peripherals_.interrupts() & ~s.active;
}
bool Memory::hasScheduledPeripheralEvent() const { return peripherals_.hasEvents(); }
bool Memory::receiveUart(unsigned index, uint8_t byte) {
    const bool accepted = peripherals_.receiveUart(index, byte); updateInterrupts(); return accepted;
}
}
