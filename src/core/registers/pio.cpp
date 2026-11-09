#include "catalog_details.hpp"

namespace picodbg {
void describePio(Chip chip, PeripheralDescription& d) {
    const bool pico2 = chip == Chip::RP2350;
    describeRegister(d, 0, "CTRL", "Mixte", "Contrôle des quatre machines PIO ; commandes de restart auto-effacées", {
        {"SM_ENABLE", 0, 4, "Un bit d'activation par machine"}, {"SM_RESTART", 4, 4, "Réinitialiser l'état interne des machines"},
        {"CLKDIV_RESTART", 8, 4, "Resynchroniser les diviseurs des machines"}});
    describeRegister(d, 4, "FSTAT", "RO", "Un bit par machine dans chaque groupe", {
        {"RXFULL", 0, 4, "FIFO RX pleines"}, {"RXEMPTY", 8, 4, "FIFO RX vides"}, {"TXFULL", 16, 4, "FIFO TX pleines"}, {"TXEMPTY", 24, 4, "FIFO TX vides"}});
    describeRegister(d, 8, "FDEBUG", "W1C", "Événements FIFO mémorisés ; écrire 1 efface", {
        {"RXSTALL", 0, 4, "PUSH bloqué : RX pleine"}, {"RXUNDER", 8, 4, "Lecture RX vide"},
        {"TXOVER", 16, 4, "Écriture TX pleine"}, {"TXSTALL", 24, 4, "PULL bloqué : TX vide"}});
    auto& level = describeRegister(d, 0xc, "FLEVEL", "RO", "Niveaux des huit FIFO");
    for (unsigned sm = 0; sm < 4; ++sm) {
        level.fields.push_back({"TX" + std::to_string(sm), sm * 8, 4, "Nombre de mots TX"});
        level.fields.push_back({"RX" + std::to_string(sm), sm * 8 + 4, 4, "Nombre de mots RX"});
    }
    for (unsigned sm = 0; sm < 4; ++sm) {
        describeRegister(d, 0x10 + sm * 4, "TXF" + std::to_string(sm), "WO", "Écrire ajoute un mot à la FIFO TX", {{"DATA", 0, 32, "Mot envoyé à la machine PIO"}});
        describeRegister(d, 0x20 + sm * 4, "RXF" + std::to_string(sm), "RO", "Lecture matérielle dépilante", {{"DATA", 0, 32, "Mot reçu de la machine PIO"}});
    }
    describeRegister(d, 0x30, "IRQ", "W1C", "Drapeaux IRQ internes", {{"IRQ", 0, 8, "Drapeaux 0 à 7 ; ne sont pas les numéros NVIC"}});
    describeRegister(d, 0x34, "IRQ_FORCE", "WO", "Forcer les drapeaux IRQ internes", {{"IRQ_FORCE", 0, 8, "Un bit par drapeau"}});
    describeRegister(d, 0x38, "INPUT_SYNC_BYPASS", "RW", "Court-circuiter les synchroniseurs d'entrée GPIO");
    describeRegister(d, 0x3c, "DBG_PADOUT", "RO", "Sorties PIO avant multiplexage GPIO");
    describeRegister(d, 0x40, "DBG_PADOE", "RO", "Autorisations de sortie PIO avant multiplexage");
    describeRegister(d, 0x44, "DBG_CFGINFO", "RO", "Capacités du bloc PIO");
    for (unsigned i = 0; i < 32; ++i)
        describeRegister(d, 0x48 + i * 4, "INSTR_MEM" + std::to_string(i), "WO", "Mémoire d'instructions partagée par les quatre machines", {{"INSTRUCTION", 0, 16, "Encodage PIO (distinct d'ARM/Thumb)"}});
    for (unsigned sm = 0; sm < 4; ++sm) {
        const uint32_t at = 0xc8 + sm * 0x18;
        const auto name = "SM" + std::to_string(sm) + "_";
        describeRegister(d, at, name + "CLKDIV", "RW", "Diviseur d'horloge de la machine", {{"FRAC", 8, 8, "Fraction en unités de 1/256"}, {"INT", 16, 16, "Partie entière ; 0 représente 65536"}});
        describeRegister(d, at + 4, name + "EXECCTRL", "Mixte", "Configuration d'exécution ; EXEC_STALLED RO", {
            {"WRAP_BOTTOM", 7, 5, "Première instruction de la boucle"}, {"WRAP_TOP", 12, 5, "Dernière instruction de la boucle"},
            {"JMP_PIN", 24, 5, "Entrée utilisée par JMP PIN"}, flag("EXEC_STALLED", 31, "Instruction injectée bloquée")});
        describeRegister(d, at + 8, name + "SHIFTCTRL", "RW", "Registres à décalage et FIFO", {
            flag("AUTOPUSH", 16, "PUSH automatique"), flag("AUTOPULL", 17, "PULL automatique"),
            flag("IN_SHIFTDIR", 18, "Sens IN", "Gauche", "Droite"), flag("OUT_SHIFTDIR", 19, "Sens OUT", "Gauche", "Droite"),
            {"PUSH_THRESH", 20, 5, "Seuil PUSH en bits ; 0 représente 32"}, {"PULL_THRESH", 25, 5, "Seuil PULL en bits ; 0 représente 32"},
            flag("FJOIN_TX", 30, "Fusion FIFO vers TX"), flag("FJOIN_RX", 31, "Fusion FIFO vers RX")});
        describeRegister(d, at + 12, name + "ADDR", "RO", "Compteur de programme PIO", {{"PC", 0, 5, "Index dans INSTR_MEM"}});
        describeRegister(d, at + 16, name + "INSTR", "RW", "Lecture : instruction en cours ; écriture : exécution injectée", {{"INSTRUCTION", 0, 16, "Encodage PIO"}});
        describeRegister(d, at + 20, name + "PINCTRL", "RW", "Correspondance des groupes de broches", {
            {"OUT_BASE", 0, 5, "Première broche OUT"}, {"SET_BASE", 5, 5, "Première broche SET"}, {"SIDESET_BASE", 10, 5, "Première broche side-set"},
            {"IN_BASE", 15, 5, "Première broche IN"}, {"OUT_COUNT", 20, 6, "Nombre de broches OUT"}, {"SET_COUNT", 26, 3, "Nombre de broches SET"}, {"SIDESET_COUNT", 29, 3, "Nombre de bits side-set"}});
    }
    if (pico2) describeRegister(d, 0x168, "GPIOBASE", "RW", "Base GPIO du bloc PIO RP2350", {{"GPIOBASE", 4, 1, "Décalage des indices de broches", {{0, "GPIO 0–31"}, {1, "GPIO 16–47"}}}});
    const uint32_t irq = pico2 ? 0x16c : 0x128;
    const std::vector<RegisterField> irqFields = {{"RXNEMPTY", 0, 4, "FIFO RX non vides"}, {"TXNFULL", 4, 4, "FIFO TX non pleines"}, {"SM", 8, pico2 ? 8u : 4u, "Drapeaux IRQ internes"}};
    describeRegister(d, irq, "INTR", "RO", "Interruptions brutes", irqFields);
    const char* suffix[] = {"INTE", "INTF", "INTS"};
    for (unsigned line = 0; line < 2; ++line) for (unsigned n = 0; n < 3; ++n)
        describeRegister(d, irq + 4 + line * 12 + n * 4, "IRQ" + std::to_string(line) + "_" + suffix[n], n == 2 ? "RO" : "RW", "IRQ PIO : masque, forçage ou état masqué", irqFields);
}
}
