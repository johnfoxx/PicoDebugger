#include "picodbg/debugger.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace picodbg {
namespace {
// Small, read-only expression language. It deliberately cannot call target code
// or perform assignments: refreshing a watch must not mutate a debug session.
class Expression {
public:
    Expression(const Debugger& debugger, unsigned core, const std::string& text)
        : debugger_(debugger), core_(core), text_(text) {}
    uint32_t evaluate() {
        const auto value = binary(0); space();
        if (pos_ != text_.size()) fail("Texte inattendu");
        return value;
    }
private:
    const Debugger& debugger_;
    unsigned core_;
    const std::string& text_;
    size_t pos_ = 0;
    unsigned depth_ = 0;
    void space() { while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_; }
    void fail(const std::string& reason) const { throw Error(ErrorKind::InvalidArgument, reason + " dans l'expression à la position " + std::to_string(pos_)); }
    bool take(const std::string& token) { space(); if (text_.compare(pos_, token.size(), token) == 0) { pos_ += token.size(); return true; } return false; }
    uint32_t atom() {
        if (++depth_ > 64) fail("Expression trop imbriquée");
        const auto result = atomImpl(); --depth_; return result;
    }
    uint32_t atomImpl() {
        space();
        if (take("(")) { const auto n = binary(0); if (!take(")")) fail("Parenthèse attendue"); return n; }
        if (take("~")) return ~atom();
        if (take("-")) return 0u - atom();
        if (take("+")) return atom();
        if (take("*")) return debugger_.memory.peek(atom(), 4, core_);
        space();
        const size_t start = pos_;
        while (pos_ < text_.size()) {
            const auto c = static_cast<unsigned char>(text_[pos_]);
            if (!std::isalnum(c) && c != '_' && c != '$' && c != ':' && c != '.') break;
            ++pos_;
        }
        if (start == pos_) fail("Valeur attendue");
        std::string name = text_.substr(start, pos_ - start);
        if (std::isdigit(static_cast<unsigned char>(name[0]))) {
            try {
                size_t used = 0;
                const uint64_t n = std::stoull(name, &used, name.compare(0, 2, "0x") == 0 || name.compare(0, 2, "0X") == 0 ? 16 : 10);
                if (used != name.size() || n > 0xffffffffull) fail("Entier 32 bits attendu");
                return static_cast<uint32_t>(n);
            } catch (const std::invalid_argument&) { fail("Entier invalide"); }
              catch (const std::out_of_range&) { fail("Entier trop grand"); }
        }
        if (name == "read8" || name == "read16" || name == "read32") {
            if (!take("(")) fail("Parenthèse attendue");
            const auto address = binary(0);
            if (!take(")")) fail("Parenthèse attendue");
            return debugger_.memory.peek(address, name == "read8" ? 1u : name == "read16" ? 2u : 4u, core_);
        }
        if (!name.empty() && name[0] == '$') name.erase(0, 1);
        const auto& cpu = debugger_.cpus.at(core_);
        for (unsigned i = 0; i < 16; ++i) if (name == registerName(i) || name == "r" + std::to_string(i)) return cpu.r[i];
        if (name == "xpsr") return cpu.xpsr;
        if (name == "msp") return cpu.msp;
        if (name == "psp") return cpu.psp;
        if (name == "control") return cpu.control;
        if (name == "primask") return cpu.primask;
        const auto symbol = debugger_.image.symbols.find(name);
        if (symbol != debugger_.image.symbols.end()) return symbol->second;
        fail("Symbole inconnu : " + name); return 0;
    }
    uint32_t binary(unsigned level) {
        if (level == 6) return atom();
        uint32_t lhs = binary(level + 1);
        while (true) {
            std::string op;
            switch (level) {
            case 0: if (take("|")) op = "|"; break;
            case 1: if (take("^")) op = "^"; break;
            case 2: if (take("&")) op = "&"; break;
            case 3: if (take("<<")) op = "<<"; else if (take(">>")) op = ">>"; break;
            case 4: if (take("+")) op = "+"; else if (take("-")) op = "-"; break;
            case 5: if (take("*")) op = "*"; else if (take("/")) op = "/"; else if (take("%")) op = "%"; break;
            }
            if (op.empty()) return lhs;
            const uint32_t rhs = binary(level + 1);
            if (op == "|") lhs |= rhs;
            else if (op == "^") lhs ^= rhs;
            else if (op == "&") lhs &= rhs;
            else if (op == "+") lhs += rhs;
            else if (op == "-") lhs -= rhs;
            else if (op == "*") lhs *= rhs;
            else if (op == "/" || op == "%") { if (!rhs) fail("Division par zéro"); lhs = op == "/" ? lhs / rhs : lhs % rhs; }
            else { if (rhs >= 32) fail("Décalage hors 0..31"); lhs = op == "<<" ? lhs << rhs : lhs >> rhs; }
        }
    }
};
}
Debugger::Debugger(Chip chip) : memory(chip) {
    memory.onAccess = [this](const MemoryAccess& access) {
        if (access.kind == Access::Execute) return;
        for (const auto& watch : watchpoints) {
            const bool overlap = uint64_t(access.address) < uint64_t(watch.address) + watch.length &&
                                 uint64_t(watch.address) < uint64_t(access.address) + access.size;
            if (overlap && ((watch.read && access.kind == Access::Read) || (watch.write && access.kind == Access::Write))) {
                watchHit_ = true; lastAccess_ = access; return;
            }
        }
    };
}
void Debugger::load(const std::string& path) { loadImage(ElfImage::read(path)); }
void Debugger::loadImage(const ElfImage& candidate) {
    // Validate into a temporary bus. A rejected ELF leaves the current session
    // intact, including watches, memory and both CPU contexts.
    Memory probe(memory.description().chip);
    candidate.install(probe);
    uint32_t entry = candidate.entry;
    if (candidate.hasVectors) {
        if (candidate.vectorAddress & 0xffu) throw Error(ErrorKind::InvalidImage, "Table de vecteurs non alignée sur 256 octets");
        entry = probe.peek(candidate.vectorAddress + 4, 4);
        const uint32_t sp = probe.peek(candidate.vectorAddress, 4);
        if ((sp & 3u) || sp < 0x20000020u || sp > 0x20000000u + memory.description().ramSize)
            throw Error(ErrorKind::InvalidImage, "SP initial hors SRAM / non aligné");
    }
    if (!(entry & 1u)) throw Error(ErrorKind::InvalidImage, "Point d'entrée Thumb requis (bit 0 à 1)");
    probe.read(entry & ~1u, 2, 0, Access::Execute);
    image = candidate; loaded_ = true;
    breakpoints.clear(); watchpoints.clear(); reset();
}
void Debugger::reset() {
    if (!loaded_) throw Error(ErrorKind::InvalidArgument, "Charger un ELF avant reset");
    memory.clear(); image.install(memory);
    const uint32_t entry = image.hasVectors ? memory.peek(image.vectorAddress + 4, 4) : image.entry;
    const uint32_t sp = image.hasVectors ? memory.peek(image.vectorAddress, 4) : 0x20000000u + memory.description().ramSize;
    cpus = {}; cpus[0].reset(entry, sp);
    memory.system(0).vtor = memory.system(1).vtor = image.hasVectors ? image.vectorAddress : 0;
    terminal_ = watchHit_ = skipBreakpoint_ = false; nextCore_ = 0; ++revision_;
    stop.kind = StopKind::Ready; stop.core = 0; stop.address = entry & ~1u;
    stop.message = image.hasVectors ? "Reset du modèle : vecteurs ELF, cœur 1 retenu, Boot ROM non exécutée" :
        "Démarrage direct à e_entry, SP au sommet SRAM (pas de vecteurs identifiés)";
}
void Debugger::startCore(unsigned core, uint32_t entry, uint32_t stack) {
    if (terminal_) throw Error(ErrorKind::InvalidArgument, "Reset requis avant de redémarrer un cœur après un arrêt terminal");
    if (!loaded_ || core > 1 || !(entry & 1u) || (stack & 3u) || stack < 0x20000020u || stack > 0x20000000u + memory.description().ramSize)
        throw Error(ErrorKind::InvalidArgument, "Cœur, entrée Thumb ou pile invalide");
    memory.read(entry & ~1u, 2, core, Access::Execute);
    cpus[core].reset(entry, stack); ++revision_;
    stop.kind = StopKind::Ready; stop.core = core; stop.address = entry & ~1u;
    stop.message = "Cœur " + std::to_string(core) + " démarré explicitement ; protocole ROM non émulé";
}
Stop Debugger::step(unsigned core) {
    skipBreakpoint_ = false;
    return stepImpl(core);
}
Stop Debugger::stepImpl(unsigned core) {
    if (!loaded_ || core > 1) throw Error(ErrorKind::InvalidArgument, "Session ou cœur invalide");
    if (terminal_) return stop;
    Cpu& cpu = cpus[core];
    if (cpu.held || cpu.lockup) {
        stop.kind = StopKind::Idle; stop.core = core; stop.address = cpu.r[15];
        stop.message = cpu.lockup ? "Cœur en lockup" : "Cœur retenu : démarrage explicite nécessaire";
        return stop;
    }
    watchHit_ = false;
    const uint32_t pc = cpu.r[15];
    stop.kind = StopKind::Step; stop.core = core; stop.address = pc; stop.message = "Pas instruction";
    try {
        const StepResult result = execute(cpu, memory, core);
        // One virtual microsecond per scheduler quantum. This is deliberately
        // independent of the host clock and is not an ARM cycle timing claim.
        memory.advanceTime(1);
        // Deterministic virtual quanta, NOT hardware CPU cycles. Both SysTicks
        // advance even while only one core is selected for manual stepping.
        memory.tick(0); memory.tick(1);
        if (result.sendEvent) for (auto& c : cpus) c.event = true;
        if (result.breakpoint) { stop.kind = StopKind::Breakpoint; stop.message = result.detail; }
        else if (result.exception) { stop.kind = StopKind::Step; stop.message = result.detail; }
        if (cpu.lockup) { terminal_ = true; stop.kind = StopKind::Fault; stop.message = "Lockup : " + cpu.lastFault; }
        if (watchHit_) {
            stop.kind = StopKind::Watchpoint;
            stop.message = std::string(lastAccess_.kind == Access::Write ? "Écriture" : "Lecture") + " de " + std::to_string(lastAccess_.size) + " octets à " + hex(lastAccess_.address) + " (arrêt après instruction)";
        }
    } catch (const Error& error) {
        cpu.r[15] = pc;
        if (error.kind == ErrorKind::Unsupported) {
            stop.kind = StopKind::Unsupported; terminal_ = true;
            stop.message = std::string(error.what()) + " à " + hex(error.address) + "; reset requis";
        } else if (error.kind == ErrorKind::BusFault || error.kind == ErrorKind::Alignment) {
            cpu.faultAddress = error.address; cpu.lastFault = error.what();
            stop.kind = StopKind::Fault;
            try { enterException(cpu, memory, core, 3, pc); }
            catch (const Error& nested) { terminal_ = true; stop.kind = StopKind::Unsupported; cpu.lastFault += "; " + std::string(nested.what()); }
            terminal_ = terminal_ || cpu.lockup;
            stop.message = cpu.lastFault + (cpu.lockup ? " — LOCKUP" : stop.kind == StopKind::Fault ? " — entrée HardFault, reprise possible" : " — modèle incomplet");
        } else throw;
    }
    ++revision_;
    return stop;
}
Stop Debugger::run(size_t budget) {
    if (!loaded_) throw Error(ErrorKind::InvalidArgument, "Charger un ELF avant exécution");
    if (terminal_) return stop;
    for (size_t n = 0; n < budget; ++n) {
        const unsigned core = nextCore_;
        nextCore_ = 1u - nextCore_;
        if (cpus[core].held || cpus[core].lockup) {
            if ((cpus[0].held || cpus[0].lockup) && (cpus[1].held || cpus[1].lockup)) break;
            continue;
        }
        const uint32_t pc = cpus[core].r[15];
        if (breakpoints.count(pc) && !cpus[core].sleeping) {
            if (skipBreakpoint_ && skipCore_ == core && skipAddress_ == pc) skipBreakpoint_ = false;
            else {
                stop.kind = StopKind::Breakpoint; stop.core = core; stop.address = pc; stop.message = "Point d'arrêt avant instruction";
                skipBreakpoint_ = true; skipCore_ = core; skipAddress_ = pc; nextCore_ = core;
                return stop;
            }
        }
        const auto result = stepImpl(core);
        if (result.kind == StopKind::Breakpoint || result.kind == StopKind::Watchpoint || result.kind == StopKind::Fault || result.kind == StopKind::Unsupported) return stop;
        bool idle = !memory.hasScheduledPeripheralEvent();
        for (unsigned i = 0; i < 2; ++i) {
            const auto& c = cpus[i]; const auto& s = memory.system(i);
            if (!c.held && !c.lockup && (!c.sleeping || c.event || s.systickPending || (s.enabled & s.pending) || (s.systickCtrl & 1u))) idle = false;
        }
        if (idle) { stop.kind = StopKind::Idle; stop.message = "Cœurs endormis ; aucune source d'événement modélisée active"; return stop; }
    }
    stop.kind = StopKind::Budget; stop.message = "Tranche d'exécution terminée (ordonnancement alterné déterministe)";
    return stop;
}
void Debugger::pause() { if (!terminal_) { stop.kind = StopKind::User; stop.message = "Pause demandée"; } }
uint32_t Debugger::evaluate(const std::string& expression, unsigned core) const {
    if (core > 1 || expression.size() > 4096) throw Error(ErrorKind::InvalidArgument, "Expression/cœur invalide");
    return Expression(*this, core, expression).evaluate();
}
void Debugger::addWatchpoint(uint32_t address, uint32_t length, bool read, bool write) {
    if (!length || uint64_t(address) + length > 0x100000000ull || (!read && !write))
        throw Error(ErrorKind::InvalidArgument, "Plage ou mode de watchpoint invalide");
    watchpoints.push_back({address, length, read, write});
}
std::string Debugger::sourceHint(uint32_t address) const { return image.symbolAt(address); }
std::string stopName(StopKind kind) {
    switch (kind) {
    case StopKind::Ready: return "PRÊT"; case StopKind::Step: return "PAS";
    case StopKind::Breakpoint: return "BREAKPOINT"; case StopKind::Watchpoint: return "WATCHPOINT";
    case StopKind::Fault: return "FAUTE CPU"; case StopKind::Unsupported: return "NON ÉMULÉ";
    case StopKind::Idle: return "ATTENTE"; case StopKind::User: return "PAUSE"; case StopKind::Budget: return "TRANCHE";
    }
    return "?";
}
ElfImage demoImage() {
    ElfImage image;
    image.entry = 0x10000201; image.vectorAddress = 0x10000100; image.hasVectors = true;
    Segment segment; segment.address = 0x10000100; segment.bytes.resize(0x160, 0);
    const auto word = [&segment](size_t offset, uint32_t value) { for (unsigned i = 0; i < 4; ++i) segment.bytes.at(offset + i) = uint8_t(value >> (8 * i)); };
    const auto half = [&segment](size_t offset, uint16_t value) { segment.bytes.at(offset) = uint8_t(value); segment.bytes.at(offset + 1) = uint8_t(value >> 8); };
    word(0, 0x20041000); word(4, image.entry); word(12, 0x10000241); word(44, 0x10000241); word(60, 0x10000241);
    const uint16_t code[] = {0x2000, 0x4903, 0x3001, 0x6008, 0x2805, 0xd1fb, 0xbe00, 0xe7fe};
    for (size_t i = 0; i < 8; ++i) half(0x100 + 2*i, code[i]);
    word(0x110, 0x20000000);
    const uint16_t core1[] = {0x2000, 0x4903, 0x3002, 0x6008, 0x280a, 0xd1fb, 0xbe01, 0xe7fe};
    for (size_t i = 0; i < 8; ++i) half(0x120 + 2*i, core1[i]);
    word(0x130, 0x20000004); half(0x140, 0xbe02); half(0x142, 0x4770);
    image.segments.push_back(segment);
    image.symbols = {{"Reset_Handler", image.entry}, {"core1_entry", 0x10000221}, {"counter", 0x20000000}, {"counter1", 0x20000004}, {"__vectors", image.vectorAddress}};
    image.functions = {{0x10000200, "Reset_Handler"}, {0x10000220, "core1_entry"}, {0x10000240, "Fault_Handler"}};
    return image;
}
}
