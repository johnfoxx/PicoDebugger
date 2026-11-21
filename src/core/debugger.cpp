#include "picodbg/debugger.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace picodbg {
Debugger::Debugger(Chip chip) : Debugger(defaultBoard(chip)) {}
Debugger::Debugger(Board board) : memory(describeBoard(board).chip), board_(describeBoard(board)) {
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
    terminal_ = watchHit_ = skipBreakpoint_ = false; nextCore_ = 0; virtualTime_ = 0; ++revision_;
    stop.kind = StopKind::Ready; stop.core = 0; stop.address = entry & ~1u;
    stop.message = std::string("ELF direct : PC=") + hex(entry & ~1u) + " SP=" + hex(sp) +
        (image.hasVectors ? " VTOR=" + hex(image.vectorAddress) : " ; pas de vecteurs identifiés") +
        " ; Boot ROM non exécutée, resets périphériques relâchés, cœur 1 retenu ; carte " + board_.name +
        (board_.wireless ? " ; radio/LED CYW43 non émulées" : "");
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
        if (virtualTime_ == std::numeric_limits<uint64_t>::max())
            throw Error(ErrorKind::InvalidArgument, "Temps virtuel maximal atteint ; reset requis");
        const StepResult result = execute(cpu, memory, core);
        // One virtual microsecond per scheduler quantum. This is deliberately
        // independent of the host clock and is not an ARM cycle timing claim.
        clockQuantum();
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
            if ((cpus[0].held || cpus[0].lockup) && (cpus[1].held || cpus[1].lockup)) {
                stop.kind = StopKind::Idle; stop.message = "Aucun cœur exécutable"; return stop;
            }
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
void Debugger::clockQuantum() {
    memory.advanceTime(1);
    // The same clock policy applies to manual time and instruction execution.
    // No CPU instructions or interrupt handlers run during a manual advance.
    memory.tick(0); memory.tick(1); ++virtualTime_;
}
void Debugger::advanceTime(uint64_t microseconds) {
    if (!loaded_ || terminal_) throw Error(ErrorKind::InvalidArgument, "Session indisponible pour avancer le temps");
    // Keep synchronous calls bounded. Frontends split longer requests and
    // return to their event loop between chunks, so Pause remains available.
    if (microseconds > 1000000 || microseconds > std::numeric_limits<uint64_t>::max() - virtualTime_)
        throw Error(ErrorKind::InvalidArgument, "Avance limitée à 1 000 000 µs par appel, sans débordement");
    if (!microseconds) return;
    for (uint64_t i = 0; i < microseconds; ++i) clockQuantum();
    ++revision_;
    // Preserve the breakpoint resume token: CPU state has not moved.
    stop.kind = StopKind::User; stop.message = "CPU figés ; temps virtuel = " + std::to_string(virtualTime_) + " µs";
}
Stop Debugger::runFor(uint64_t microseconds, size_t budget) {
    if (!loaded_) throw Error(ErrorKind::InvalidArgument, "Charger un ELF avant exécution");
    if (terminal_) return stop;
    if (microseconds > std::numeric_limits<uint64_t>::max() - virtualTime_)
        throw Error(ErrorKind::InvalidArgument, "Durée virtuelle débordante");
    const uint64_t target = virtualTime_ + microseconds;
    for (size_t i = 0; i < budget && virtualTime_ < target; ++i) {
        if (run(1).kind != StopKind::Budget) return stop;
    }
    if (virtualTime_ == target) {
        stop.kind = StopKind::User; stop.message = "Durée virtuelle atteinte ; exécution en pause";
    } else { stop.kind = StopKind::Budget; stop.message = "Durée virtuelle en cours"; }
    return stop;
}
size_t Debugger::injectUart(unsigned index, const std::string& bytes) {
    if (!loaded_ || terminal_ || index > 1) throw Error(ErrorKind::InvalidArgument, "Session/UART indisponible");
    size_t accepted = 0;
    for (unsigned char byte : bytes) if (memory.receiveUart(index, byte)) ++accepted;
    ++revision_; // An RX overflow also changes state, even if no byte was accepted.
    return accepted;
}
void Debugger::addWatchpoint(uint32_t address, uint32_t length, bool read, bool write) {
    if (!length || uint64_t(address) + length > 0x100000000ull || (!read && !write))
        throw Error(ErrorKind::InvalidArgument, "Plage ou mode de watchpoint invalide");
    watchpoints.push_back({address, length, read, write});
}
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
    // Même programme Thumb que dans examples/make_demo.py, chargé directement en mémoire.
    ElfImage image;
    image.entry = 0x10000201; image.vectorAddress = 0x10000100; image.hasVectors = true;
    Segment segment; segment.address = 0x10000100; segment.bytes.resize(0x160, 0);
    const auto word = [&segment](size_t offset, uint32_t value) { for (unsigned i = 0; i < 4; ++i) segment.bytes.at(offset + i) = uint8_t(value >> (8 * i)); };
    const auto half = [&segment](size_t offset, uint16_t value) { segment.bytes.at(offset) = uint8_t(value); segment.bytes.at(offset + 1) = uint8_t(value >> 8); };
    // Vecteurs MSP, Reset, HardFault, SVC et SysTick. Les adresses de code marquent Thumb.
    word(0, 0x20041000); word(4, image.entry); word(12, 0x10000241); word(44, 0x10000241); word(60, 0x10000241);
    const uint16_t code[] = {
        0x2000, // MOVS r0, #0       Initialise le compteur du cœur 0.
        0x4903, // LDR r1, [pc, #12] Charge l'adresse du compteur.
        0x3001, // ADDS r0, #1       Incrémente le compteur.
        0x6008, // STR r0, [r1]      Enregistre la valeur en SRAM.
        0x2805, // CMP r0, #5        Détecte la valeur finale.
        0xd1fb, // BNE boucle        Revient à ADDS tant que le compteur diffère de 5.
        0xbe00, // BKPT #0           Marque l'arrêt du cœur 0.
        0xe7fe  // B .               Boucle après une reprise.
    };
    for (size_t i = 0; i < 8; ++i) half(0x100 + 2*i, code[i]);
    word(0x110, 0x20000000); // Littéral lu par le LDR du cœur 0.
    // Le cœur 1 suit la même séquence. Il ajoute 2 jusqu'à 10, puis exécute BKPT #1.
    const uint16_t core1[] = {0x2000, 0x4903, 0x3002, 0x6008, 0x280a, 0xd1fb, 0xbe01, 0xe7fe};
    for (size_t i = 0; i < 8; ++i) half(0x120 + 2*i, core1[i]);
    word(0x130, 0x20000004); // Adresse distincte pour le compteur du cœur 1.
    // Fault_Handler signale l'exception avec BKPT #2. BX LR permet le retour.
    half(0x140, 0xbe02); half(0x142, 0x4770);
    image.segments.push_back(segment);
    image.symbols = {{"Reset_Handler", image.entry}, {"core1_entry", 0x10000221}, {"counter", 0x20000000}, {"counter1", 0x20000004}, {"__vectors", image.vectorAddress}};
    image.functions = {{0x10000200, "Reset_Handler"}, {0x10000220, "core1_entry"}, {0x10000240, "Fault_Handler"}};
    return image;
}
}
