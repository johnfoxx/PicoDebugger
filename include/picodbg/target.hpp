#pragma once
#include "picodbg/cpu.hpp"
#include "picodbg/elf.hpp"
#include "picodbg/board.hpp"
#include <set>

namespace picodbg {
enum class StopKind { Ready, Step, Breakpoint, Watchpoint, Fault, Unsupported, Idle, User, Budget };
struct Stop {
    StopKind kind = StopKind::Ready;
    unsigned core = 0;
    uint32_t address = 0;
    std::string message = "Aucun exécutable chargé";
};
struct Watchpoint {
    uint32_t address;
    uint32_t length;
    bool read, write;
};

// Shared frontend contract. Hardware implementations contain snapshots only;
// execution and reads must go through the selected target, never an emulator.
class DebugTarget {
public:
    virtual ~DebugTarget() {}
    virtual void load(const std::string& path) = 0;
    virtual void loadImage(const ElfImage& image) = 0;
    virtual void reset() = 0;
    virtual void startCore(unsigned core, uint32_t entry, uint32_t stack) = 0;
    virtual Stop step(unsigned core) = 0;
    virtual Stop run(size_t budget) = 0;
    virtual Stop runFor(uint64_t microseconds, size_t budget = 1000) = 0;
    virtual void advanceTime(uint64_t microseconds) = 0;
    virtual uint64_t virtualTime() const = 0;
    virtual const BoardDescription& board() const = 0;
    virtual const ChipDescription& description() const = 0;
    virtual uint32_t peek(uint32_t address, unsigned size, unsigned core = 0) const = 0;
    virtual Instruction disassemble(uint32_t address, unsigned core) = 0;
    virtual void pause() = 0;
    virtual size_t injectUart(unsigned index, const std::string& bytes) = 0;
    virtual std::string uartOutput(unsigned index) const = 0;
    virtual void addWatchpoint(uint32_t address, uint32_t length, bool read, bool write) = 0;
    virtual void clearWatchpoints() { watchpoints.clear(); }
    virtual void removeWatchpoint(size_t index);
    virtual void addBreakpoint(uint32_t address) { breakpoints.insert(address & ~1u); }
    virtual void removeBreakpoint(uint32_t address) { breakpoints.erase(address & ~1u); }
    virtual bool loaded() const = 0;
    virtual bool terminalStop() const = 0;
    virtual uint64_t revision() const = 0;
    virtual bool hardware() const { return false; }
    virtual bool busy() const { return false; }
    virtual bool executing() const { return false; }
    virtual bool registerAvailable(unsigned core, unsigned index) const { return loaded() && core < 2 && index < 21; }
    virtual void readRegisterOnce(uint32_t, unsigned) const {}
    uint32_t registerValue(unsigned core, unsigned index) const;
    uint32_t evaluate(const std::string& expression, unsigned core = 0) const;
    std::string sourceHint(uint32_t address) const { return image.symbolAt(address); }
    std::array<Cpu, 2> cpus;
    ElfImage image;
    std::set<uint32_t> breakpoints;
    std::vector<Watchpoint> watchpoints;
    Stop stop;
};
std::string stopName(StopKind kind);
}
