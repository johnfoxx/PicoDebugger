#pragma once
#include "picodbg/cpu.hpp"
#include "picodbg/elf.hpp"
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

// All mutations are synchronous and performed by one owner (CLI or GUI event
// loop). A GUI runs bounded batches, then renders a consistent frozen snapshot.
class Debugger {
public:
    explicit Debugger(Chip chip = Chip::RP2040);
    Debugger(const Debugger&) = delete;
    Debugger& operator=(const Debugger&) = delete;
    void load(const std::string& path);
    void loadImage(const ElfImage& image);
    void reset();
    void startCore(unsigned core, uint32_t entry, uint32_t stack);
    Stop step(unsigned core);
    Stop run(size_t budget);
    void pause();
    uint32_t evaluate(const std::string& expression, unsigned core = 0) const;
    void addWatchpoint(uint32_t address, uint32_t length, bool read, bool write);
    void clearWatchpoints() { watchpoints.clear(); }
    bool loaded() const { return loaded_; }
    bool terminalStop() const { return terminal_; }
    uint64_t revision() const { return revision_; }
    std::string sourceHint(uint32_t address) const;
    Memory memory;
    std::array<Cpu, 2> cpus;
    ElfImage image;
    std::set<uint32_t> breakpoints;
    std::vector<Watchpoint> watchpoints;
    Stop stop;
private:
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
