#pragma once
#include "picodbg/target.hpp"

namespace picodbg {
// All mutations are synchronous and performed by one owner. The GUI event
// loop runs bounded batches, then renders a consistent frozen snapshot.
class Debugger : public DebugTarget {
public:
    explicit Debugger(Chip chip = Chip::RP2040);
    explicit Debugger(Board board);
    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;
    void load(const std::string& path);
    void loadImage(const ElfImage& image);
    void reset();
    void startCore(unsigned core, uint32_t entry, uint32_t stack);
    Stop step(unsigned core);
    Stop run(size_t budget);
    // Bounded operations for interactive callers. runFor executes firmware;
    // advanceTime clocks peripherals/SysTick with both CPU contexts frozen.
    Stop runFor(uint64_t microseconds, size_t budget = 1000);
    void advanceTime(uint64_t microseconds);
    uint64_t virtualTime() const { return virtualTime_; }
    const BoardDescription& board() const { return board_; }
    void pause();
    size_t injectUart(unsigned index, const std::string& bytes);
    void addWatchpoint(uint32_t address, uint32_t length, bool read, bool write);
    void clearWatchpoints() { watchpoints.clear(); }
    bool loaded() const { return loaded_; }
    bool terminalStop() const { return terminal_; }
    uint64_t revision() const { return revision_; }
    const ChipDescription& description() const override { return memory.description(); }
    uint32_t peek(uint32_t address, unsigned size, unsigned core = 0) const override { return memory.peek(address, size, core); }
    Instruction disassemble(uint32_t address, unsigned core) override { return decode(memory, address, core); }
    std::string uartOutput(unsigned index) const override { return memory.uartOutput(index); }
    Memory memory;
private:
    BoardDescription board_;
    uint64_t virtualTime_ = 0;
    void clockQuantum();
    bool loaded_ = false, terminal_ = false, watchHit_ = false;
    MemoryAccess lastAccess_ = {0, 0, Access::Read, 0};
    uint64_t revision_ = 0;
    unsigned nextCore_ = 0;
    bool skipBreakpoint_ = false;
    unsigned skipCore_ = 0;
    uint32_t skipAddress_ = 0;
    Stop stepImpl(unsigned core);
};
ElfImage demoImage();
std::string stopName(StopKind kind);
}
