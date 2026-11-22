#pragma once
#include "picodbg/chip.hpp"
#include "picodbg/peripheral_bus.hpp"
#include <array>
#include <deque>
#include <functional>
#include <vector>

namespace picodbg {
struct SystemRegisters {
    uint32_t vtor = 0;
    uint64_t enabled = 0, pending = 0, active = 0;
    uint32_t systickCtrl = 0, systickLoad = 0, systickValue = 0;
    bool systickPending = false, pendSv = false, systickCountFlag = false;
};

class Memory {
public:
    explicit Memory(Chip chip = Chip::RP2040);
    const ChipDescription& description() const { return description_; }
    uint32_t read(uint32_t address, unsigned size, unsigned core = 0, Access access = Access::Read);
    void write(uint32_t address, uint32_t value, unsigned size, unsigned core = 0);
    // Inspection never consumes FIFO words, clears status flags or takes a lock.
    uint32_t peek(uint32_t address, unsigned size, unsigned core = 0) const;
    // A debugger write uses bus semantics without reporting a CPU access.
    void poke(uint32_t address, uint32_t value, unsigned size, unsigned core = 0);
    void load(uint32_t address, const std::vector<uint8_t>& bytes);
    void clear();
    void tick(unsigned core);
    void advanceTime(uint64_t virtualMicroseconds);
    bool hasScheduledPeripheralEvent() const;
    bool receiveUart(unsigned index, uint8_t byte);
    const std::string& uartOutput(unsigned index) const { return peripherals_.uartOutput(index); }
    void updateInterrupts();
    SystemRegisters& system(unsigned core) { return system_.at(core); }
    const SystemRegisters& system(unsigned core) const { return system_.at(core); }
    std::function<void(const MemoryAccess&)> onAccess;
private:
    ChipDescription description_;
    std::vector<uint8_t> ram_, flash_;
    std::array<SystemRegisters, 2> system_;
    std::array<std::deque<uint32_t>, 2> fifo_;
    std::array<bool, 32> locks_;
    std::array<uint32_t, 2> fifoErrors_;
    PeripheralBus peripherals_;
    uint32_t ramOffset(uint32_t address) const;
    uint32_t readImpl(uint32_t address, unsigned size, unsigned core, Access access, bool inspect);
    uint32_t readDevice(uint32_t address, unsigned core, bool inspect);
    void writeDevice(uint32_t address, uint32_t value, unsigned core);
    void check(uint32_t address, unsigned size, unsigned core) const;
};
}
