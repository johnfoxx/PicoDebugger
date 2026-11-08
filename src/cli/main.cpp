#include "picodbg/debugger.hpp"
#include <csignal>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace picodbg;
namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
void help() {
    std::cout <<
        "PicoDebugger — simulation ARM/Thumb partielle, sans carte\n"
        "  load chemin.elf           charger ELF32 ARM, reste à l'arrêt\n"
        "  demo                      charger l'exemple embarqué\n"
        "  step [nombre] / s         pas instruction du cœur sélectionné\n"
        "  run [quanta] / c          alterner les cœurs (défaut 100000), Ctrl-C = pause\n"
        "  reset / restart           recharger l'image initiale, arrêter avant exécution\n"
        "  core 0|1                  sélectionner le contexte affiché\n"
        "  launch cœur entrée sp     démarrage explicite d'un cœur (entrée Thumb impaire)\n"
        "  regs / r                  registres et flags des DEUX cœurs\n"
        "  asm [adresse] [nombre]    désassembler (défaut PC, 12 instructions)\n"
        "  mem adresse [octets]      mémoire hex/ASCII (défaut 128 octets)\n"
        "  stack [mots]              pile brute à partir du SP (défaut 16 mots)\n"
        "  map                       cartographie et couverture réelle\n"
        "  break expression          point d'arrêt avant instruction\n"
        "  delete expression         supprimer un point d'arrêt\n"
        "  watch adresse taille r|w|rw  arrêt APRÈS accès mémoire\n"
        "  unwatch                   supprimer tous les watchpoints mémoire\n"
        "  display expression        expression actualisée après chaque commande\n"
        "  undisplay                 supprimer les expressions surveillées\n"
        "  print expression / p      évaluer sans modifier le CPU\n"
        "  points                    lister breakpoints/watchpoints\n"
        "  status                    motif d'arrêt, révision et source symbolique\n"
        "  help / quit\n"
        "Expressions : r0, $pc, sp, xpsr, symboles (adresses), + - * / % & | ^ << >>,\n"
        "              read8(adresse), read16(adresse), read32(adresse), *adresse.\n"
        "Aucune exécution automatique ; limites détaillées dans docs/COVERAGE.md.\n";
}
void registers(const Debugger& d) {
    for (unsigned core = 0; core < 2; ++core) {
        const auto& cpu = d.cpus[core];
        std::cout << "Cœur " << core << " — " << (cpu.held ? "RETENU" : cpu.lockup ? "LOCKUP" : cpu.sleeping ? "ENDORMI" : "ARRÊTÉ") << " — " << cpu.instructions << " instructions\n";
        for (unsigned i = 0; i < 16; ++i) std::cout << std::setw(3) << registerName(i) << " " << hex(cpu.r[i]) << (i % 4 == 3 ? "\n" : "   ");
        std::cout << "xPSR " << hex(cpu.xpsr) << "  " << cpu.flags() << "\nMSP " << hex(cpu.msp) << " PSP " << hex(cpu.psp)
                  << " CONTROL " << hex(cpu.control) << " PRIMASK " << cpu.primask << "\n";
    }
}
void status(const Debugger& d) {
    std::cout << "[" << stopName(d.stop.kind) << "] cœur " << d.stop.core << " à " << hex(d.stop.address)
              << " — " << d.stop.message << "\nRévision " << d.revision() << " (instantané du modèle)\n";
}
void disassemble(Debugger& d, uint32_t at, unsigned count, unsigned core) {
    for (unsigned i = 0; i < count; ++i) {
        try {
            const auto instruction = decode(d.memory, at, core);
            std::cout << (at == d.cpus[core].r[15] ? " -> " : "    ") << (d.breakpoints.count(at) ? "● " : "  ")
                      << hex(at) << "  " << hex(instruction.raw, instruction.size * 2) << "  " << instruction.text;
            const auto symbol = d.sourceHint(at);
            if (!symbol.empty()) std::cout << "  ; " << symbol;
            std::cout << "\n";
            if (uint64_t(at) + instruction.size > 0xffffffffull) break;
            at += instruction.size;
        } catch (const Error& e) { std::cout << hex(at) << "  ?? " << e.what() << "\n"; break; }
    }
}
void memory(const Debugger& d, uint32_t at, unsigned count, unsigned core) {
    if (uint64_t(at) + count > 0x100000000ull) throw Error(ErrorKind::InvalidArgument, "Plage débordante");
    for (unsigned i = 0; i < count; i += 16) {
        std::cout << hex(at + i) << "  "; std::string ascii;
        for (unsigned j = 0; j < 16 && i + j < count; ++j) {
            try {
                const uint32_t value = d.memory.peek(at + i + j, 1, core);
                std::cout << hex(value, 2).substr(2) << " "; ascii += value >= 32 && value < 127 ? char(value) : '.';
            } catch (const Error&) { std::cout << "?? "; ascii += '?'; }
        }
        std::cout << " |" << ascii << "|\n";
    }
}
unsigned boundedCount(const std::string& input, unsigned fallback, unsigned limit) {
    if (input.empty()) return fallback;
    size_t used = 0;
    const auto n = std::stoull(input, &used, 10);
    if (used != input.size() || n == 0 || n > limit) throw Error(ErrorKind::InvalidArgument, "Nombre hors limites");
    return static_cast<unsigned>(n);
}
}
int main(int argc, char** argv) {
    try {
        Chip chip = Chip::RP2040;
        std::string elf, batch;
        bool demo = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--demo") demo = true;
            else if ((arg == "--chip" || arg == "--elf" || arg == "--batch") && i + 1 < argc) {
                const std::string value = argv[++i];
                if (arg == "--elf") elf = value;
                else if (arg == "--batch") batch = value;
                else if (value == "rp2040") chip = Chip::RP2040;
                else if (value == "rp2350") chip = Chip::RP2350;
                else throw Error(ErrorKind::InvalidArgument, "Profil inconnu : " + value);
            } else if (arg == "--help" || arg == "-h") { help(); return 0; }
            else throw Error(ErrorKind::InvalidArgument, "Argument inconnu/incomplet : " + arg);
        }
        if (demo && !elf.empty()) throw Error(ErrorKind::InvalidArgument, "Choisir --demo ou --elf");
        Debugger debugger(chip);
        if (demo) debugger.loadImage(demoImage());
        if (!elf.empty()) debugger.load(elf);
        std::cout << debugger.memory.description().name << "\nÉmulation partielle : aucune garantie d'équivalence matérielle. help pour les commandes.\n";
        std::ifstream file;
        if (!batch.empty()) { file.open(batch.c_str()); if (!file) throw Error(ErrorKind::InvalidArgument, "Script introuvable : " + batch); }
        std::istream& input = batch.empty() ? std::cin : file;
        std::signal(SIGINT, interrupt);
        unsigned selected = 0;
        std::vector<std::string> displays;
        std::string line;
        status(debugger);
        while (true) {
            if (batch.empty()) std::cout << "pico[" << selected << "]> " << std::flush;
            if (!std::getline(input, line)) break;
            if (line.empty() || line[0] == '#') continue;
            std::istringstream args(line); std::string command; args >> command;
            std::string rest; std::getline(args >> std::ws, rest);
            std::istringstream words(rest);
            try {
                if (command == "quit" || command == "q" || command == "exit") break;
                else if (command == "help" || command == "h") help();
                else if (command == "load") { debugger.load(rest); status(debugger); }
                else if (command == "demo") { debugger.loadImage(demoImage()); status(debugger); }
                else if (command == "reset" || command == "restart") { debugger.reset(); status(debugger); }
                else if (command == "regs" || command == "r") registers(debugger);
                else if (command == "status") status(debugger);
                else if (command == "core") { if (rest != "0" && rest != "1") throw Error(ErrorKind::InvalidArgument, "Cœur 0 ou 1 requis"); selected = rest == "0" ? 0u : 1u; }
                else if (command == "step" || command == "s") {
                    const unsigned count = boundedCount(rest, 1, 1000000);
                    for (unsigned n = 0; n < count; ++n) { const auto s = debugger.step(selected); if (s.kind != StopKind::Step) break; }
                    status(debugger); disassemble(debugger, debugger.cpus[selected].r[15], 1, selected);
                } else if (command == "run" || command == "c") {
                    unsigned count = boundedCount(rest, 100000, 100000000);
                    interrupted = 0;
                    while (count && !interrupted) { const unsigned chunk = std::min(count, 1000u); count -= chunk; if (debugger.run(chunk).kind != StopKind::Budget) break; }
                    if (interrupted) debugger.pause();
                    status(debugger);
                } else if (command == "print" || command == "p") {
                    const uint32_t value = debugger.evaluate(rest, selected); std::cout << rest << " = " << hex(value) << " (" << value << ")\n";
                } else if (command == "display") { debugger.evaluate(rest, selected); displays.push_back(rest); }
                else if (command == "undisplay") displays.clear();
                else if (command == "break") debugger.breakpoints.insert(debugger.evaluate(rest, selected) & ~1u);
                else if (command == "delete") debugger.breakpoints.erase(debugger.evaluate(rest, selected) & ~1u);
                else if (command == "watch") {
                    std::string address, length, mode; words >> address >> length >> mode;
                    if (mode != "r" && mode != "w" && mode != "rw") throw Error(ErrorKind::InvalidArgument, "watch adresse taille r|w|rw");
                    debugger.addWatchpoint(debugger.evaluate(address, selected), debugger.evaluate(length, selected), mode != "w", mode != "r");
                } else if (command == "unwatch") debugger.clearWatchpoints();
                else if (command == "launch") {
                    std::string core, entry, sp; words >> core >> entry >> sp;
                    debugger.startCore(debugger.evaluate(core), debugger.evaluate(entry), debugger.evaluate(sp)); status(debugger);
                } else if (command == "points") {
                    for (auto address : debugger.breakpoints) std::cout << "break " << hex(address) << "\n";
                    for (const auto& w : debugger.watchpoints) std::cout << "watch " << hex(w.address) << " +" << w.length << " " << (w.read ? "r" : "") << (w.write ? "w" : "") << "\n";
                } else if (command == "map") {
                    for (const auto& region : debugger.memory.description().regions) std::cout << hex(region.begin) << "–" << hex(static_cast<uint32_t>(region.end - 1)) << " " << region.permissions << " " << region.name << "\n    " << region.coverage << "\n";
                } else if (command == "asm") {
                    std::string address, count; words >> address >> count;
                    disassemble(debugger, address.empty() ? debugger.cpus[selected].r[15] : debugger.evaluate(address, selected) & ~1u, boundedCount(count, 12, 4096), selected);
                } else if (command == "mem") {
                    std::string address, count; words >> address >> count;
                    memory(debugger, debugger.evaluate(address, selected), boundedCount(count, 128, 65536), selected);
                } else if (command == "stack") {
                    const unsigned count = boundedCount(rest, 16, 4096);
                    for (unsigned n = 0; n < count; ++n) {
                        const uint64_t at = uint64_t(debugger.cpus[selected].r[13]) + n * 4u;
                        if (at > 0xffffffffull) break;
                        try { const uint32_t value = debugger.memory.peek(static_cast<uint32_t>(at), 4, selected); std::cout << (n == 0 ? "SP → " : "     ") << hex(static_cast<uint32_t>(at)) << "  " << hex(value) << "  " << debugger.sourceHint(value & ~1u) << "\n"; }
                        catch (const Error& e) { std::cout << "     " << hex(static_cast<uint32_t>(at)) << "  ?? " << e.what() << "\n"; break; }
                    }
                } else throw Error(ErrorKind::InvalidArgument, "Commande inconnue : " + command);
                for (const auto& expression : displays) {
                    try { const uint32_t value = debugger.evaluate(expression, selected); std::cout << "watch[" << selected << "] " << expression << " = " << hex(value) << "\n"; }
                    catch (const Error& e) { std::cout << "watch " << expression << " = indisponible : " << e.what() << "\n"; }
                }
            } catch (const std::exception& e) {
                std::cerr << "Erreur : " << e.what() << "\n";
                if (!batch.empty()) return 2;
            }
        }
        return debugger.terminalStop() ? 3 : 0;
    } catch (const std::exception& e) { std::cerr << "Erreur : " << e.what() << "\n"; return 1; }
}
