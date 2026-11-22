#include "picodbg/peripherals.hpp"
#include "registers/catalog_details.hpp"
#include <algorithm>

namespace picodbg {
uint32_t fieldValue(uint32_t value, const RegisterField& field) {
    if (!field.width || field.width > 32 || field.lsb > 31 || field.width > 32 - field.lsb)
        throw Error(ErrorKind::InvalidArgument, "Champ de registre invalide");
    return field.width == 32 ? value : (value >> field.lsb) & ((1u << field.width) - 1u);
}
std::string fieldMeaning(uint32_t registerValue, const RegisterField& field) {
    const auto value = fieldValue(registerValue, field);
    for (const auto& meaning : field.meanings) if (meaning.value == value) return meaning.text;
    return field.meanings.empty() ? field.description : "Valeur non décrite pour ce champ";
}
namespace {
void timerRegisters(PeripheralDescription& device, bool pico2) {
    const char* names[] = {"TIMEHW", "TIMELW", "TIMEHR", "TIMELR", "ALARM0", "ALARM1", "ALARM2", "ALARM3", "ARMED", "TIMERAWH", "TIMERAWL", "DBGPAUSE", "PAUSE"};
    const char* access[] = {"WO", "WO", "RO", "RO", "RW", "RW", "RW", "RW", "W1C", "RO", "RO", "RW", "RW"};
    for (unsigned i = 0; i < 13; ++i)
        device.registers.push_back({device.base + 4u * i, names[i], access[i], "Compteur/alarmes ; temps virtuel fourni par le moteur", {}});
    device.registers[0].note = "Valide le compteur 64 bits après écriture de TIMELW";
    device.registers[1].note = "Latch bas ; ne change pas le compteur avant TIMEHW";
    device.registers[2].note = "Partie haute mémorisée lors de la lecture CPU de TIMELR";
    device.registers[3].note = "L'inspection ne modifie pas le latch de TIMEHR";
    device.registers[11].fields = {{"DBG0", 1, 1}, {"DBG1", 2, 1}};
    device.registers[12].fields = {{"PAUSE", 0, 1}};
    if (pico2) {
        device.registers.push_back({device.base + 0x34, "LOCKED", "RW", "Verrouille les écritures jusqu'au reset du périphérique", {{"LOCKED", 0, 1}}});
        device.registers.push_back({device.base + 0x38, "SOURCE", "RW", "Tick ou clk_sys ; deux sources virtuelles, arbre d'horloge absent", {{"CLK_SYS", 0, 1}}});
    }
    const uint32_t offset = pico2 ? 0x3cu : 0x34u;
    const char* irqNames[] = {"INTR", "INTE", "INTF", "INTS"};
    const char* irqAccess[] = {"W1C", "RW", "RW", "RO"};
    for (unsigned i = 0; i < 4; ++i) {
        device.registers.push_back({device.base + offset + i * 4u, irqNames[i], irqAccess[i], "Interruptions des quatre alarmes", {}});
        for (unsigned n = 0; n < 4; ++n) device.registers.back().fields.push_back({"ALARM" + std::to_string(n), n, 1});
    }
}
void add(PeripheralDescription& device, uint32_t offset, const char* name, const char* access, const char* note) {
    device.registers.push_back({device.base + offset, name, access, note, {}});
}
void serialRegisters(PeripheralDescription& device, bool uart) {
    const char* note = uart ? "PL011 fonctionnel ; FIFO et temps virtuel, sans signal électrique" : "PL022 fonctionnel ; loopback interne uniquement, sans esclave externe";
    if (uart) {
        add(device, 0, "UARTDR", "RW", "RX à la lecture, TX à l'écriture ; inspection sans consommer RX");
        add(device, 4, "UARTRSR/ECR", "RW", "Erreurs de réception ; écriture efface RSR");
        add(device, 0x18, "UARTFR", "RO", note);
        device.registers.back().fields = {{"BUSY", 3, 1}, {"RXFE", 4, 1}, {"TXFF", 5, 1}, {"RXFF", 6, 1}, {"TXFE", 7, 1}};
        add(device, 0x24, "UARTIBRD", "RW", "Diviseur entier stocké ; baud physique non simulé");
        add(device, 0x28, "UARTFBRD", "RW", "Diviseur fractionnaire stocké");
        add(device, 0x2c, "UARTLCR_H", "RW", note);
        device.registers.back().fields = {{"FEN", 4, 1}, {"WLEN", 5, 2}};
        add(device, 0x30, "UARTCR", "RW", note);
        device.registers.back().fields = {{"UARTEN", 0, 1}, {"LBE", 7, 1}, {"TXE", 8, 1}, {"RXE", 9, 1}};
        add(device, 0x34, "UARTIFLS", "RW", "Seuils des FIFO RX et TX");
        add(device, 0x38, "UARTIMSC", "RW", "Masque des interruptions");
        add(device, 0x3c, "UARTRIS", "RO", "RX/TX, timeout virtuel et overflow RX");
        add(device, 0x40, "UARTMIS", "RO", "Interruptions après masque");
        add(device, 0x44, "UARTICR", "WO", "Acquittement W1C des interruptions mémorisées");
        add(device, 0x48, "UARTDMACR", "RW", "DMA désactivé ; activation hors couverture");
    } else {
        const char* names[] = {"SSPCR0", "SSPCR1", "SSPDR", "SSPSR", "SSPCPSR", "SSPIMSC", "SSPRIS", "SSPMIS", "SSPICR", "SSPDMACR"};
        const char* access[] = {"RW", "RW", "RW", "RO", "RW", "RW", "RO", "RO", "WO", "RW"};
        for (unsigned i = 0; i < 10; ++i) add(device, i * 4u, names[i], access[i], note);
        device.registers[1].fields = {{"LBM", 0, 1}, {"SSE", 1, 1}, {"MS", 2, 1}};
        device.registers[3].fields = {{"TFE", 0, 1}, {"TNF", 1, 1}, {"RNE", 2, 1}, {"RFF", 3, 1}, {"BSY", 4, 1}};
    }
    device.coverage = "Partiel (FIFO, temps virtuel)"; device.note = note;
}
void pwmRegisters(PeripheralDescription& device, bool pico2) {
    const unsigned count = pico2 ? 12u : 8u;
    const char* names[] = {"CSR", "DIV", "CTR", "CC", "TOP"};
    for (unsigned i = 0; i < count; ++i) for (unsigned j = 0; j < 5; ++j) {
        const auto name = "CH" + std::to_string(i) + "_" + names[j];
        add(device, i * 0x14u + j * 4u, name.c_str(), "RW", "Compteur virtuel ; CC/TOP bufferisés pendant l'exécution");
        if (j == 0) device.registers.back().fields = {{"EN", 0, 1}, {"PH_CORRECT", 1, 1}, {"A_INV", 2, 1}, {"B_INV", 3, 1}};
        if (j == 3) device.registers.back().fields = {{"A", 0, 16}, {"B", 16, 16}};
    }
    add(device, count * 0x14u, "EN", "RW", "Activation simultanée des slices");
    add(device, count * 0x14u + 4u, "INTR", "W1C", "Wrap des compteurs");
    for (unsigned irq = 0; irq < (pico2 ? 2u : 1u); ++irq) for (unsigned j = 0; j < 3; ++j) {
        const char* suffix[] = {"INTE", "INTF", "INTS"};
        const auto name = "IRQ" + std::to_string(irq) + "_" + suffix[j];
        add(device, count * 0x14u + 8u + irq * 12u + j * 4u, name.c_str(), j == 2 ? "RO" : "RW", "Masque, forçage, état IRQ");
    }
    device.coverage = "Partiel (compteurs, IRQ)"; device.note = "PWM virtuel ; pas de pinmux, DREQ ni d'entrée B externe";
}
}
std::vector<PeripheralDescription> peripheralCatalog(Chip chip) {
    std::vector<PeripheralDescription> result;
    const auto description = describe(chip);
    const auto readable = inspectableRegisters(chip);
    for (const auto& region : description.regions) {
        if (region.permissions != "MMIO" && region.name != "USB DPRAM" && region.name != "XIP SRAM/cache") continue;
        PeripheralDescription device = {region.begin, region.name, "Non émulé", region.coverage, {}};
        if (region.begin == 0xd0000000) device.name = "SIO";
        if (region.begin == 0xe0000000) device.name = "PPB / Cortex-M";
        if (region.begin == 0x50100000) device.name = "USBCTRL_DPRAM";
        if (region.begin == 0x50110000) device.name = "USBCTRL_REGS";
        for (const auto& reg : readable) {
            if (reg.address < region.begin || reg.address >= region.end) continue;
            const bool writable = (reg.address >= 0xd0000100 && reg.address < 0xd0000180) ||
                (reg.address >= 0xd0000010 && reg.address < 0xd0000050) || region.begin == 0xe0000000;
            device.registers.push_back({reg.address, reg.name, writable ? "RW" : "RO", reg.note, {}});
            if (reg.address == 0xd0000050) device.registers.back().access = "W1C";
            if (reg.address == 0xd0000050)
                device.registers.back().fields = {{"VLD", 0, 1}, {"RDY", 1, 1}, {"WOF", 2, 1}, {"ROE", 3, 1}};
            if (reg.address == 0xe000e010)
                device.registers.back().fields = {{"ENABLE", 0, 1}, {"TICKINT", 1, 1}, {"CLKSOURCE", 2, 1}, {"COUNTFLAG", 16, 1}};
        }
        if (!device.registers.empty()) device.coverage = "Partiel";
        if (device.name == "SIO") {
            add(device, 0x54, "SIO.FIFO_WR", "WO", "Envoie un mot dans la FIFO du cœur opposé");
        }
        if (device.name == "SIO" || region.begin == 0xe0000000) {
            std::sort(device.registers.begin(), device.registers.end(), [](const RegisterDescription& a, const RegisterDescription& b) {
                return a.address < b.address;
            });
        }
        if (device.name == "TIMER" || device.name == "TIMER0" || device.name == "TIMER1") {
            device.registers.clear(); timerRegisters(device, chip == Chip::RP2350);
            device.coverage = "Partiel (temps virtuel)";
        }
        if (device.name == "UART0" || device.name == "UART1") serialRegisters(device, true);
        if (device.name == "SPI0" || device.name == "SPI1") serialRegisters(device, false);
        if (device.name == "PWM") pwmRegisters(device, chip == Chip::RP2350);
        if (device.name == "RESETS") {
            add(device, 0, "RESET", "RW", "Lignes relâchées au démarrage direct ; reset effectif des modèles présents");
            add(device, 4, "WDSEL", "RW", "Sélection stockée ; watchdog non exécuté");
            add(device, 8, "RESET_DONE", "RO", "Complétion immédiate du reset dans le modèle");
            device.coverage = "Partiel (modèles présents)";
        }
        if (device.name == "WATCHDOG") {
            for (unsigned i = 0; i < 8; ++i) add(device, 0xc + 4u * i, ("SCRATCH" + std::to_string(i)).c_str(), "RW", "Registre scratch ; compteur et reset watchdog non émulés");
            device.coverage = "Partiel (scratch uniquement)";
        }
        describePeripheralRegisters(chip, device);
        result.push_back(device);
    }
    std::sort(result.begin(), result.end(), [](const PeripheralDescription& a, const PeripheralDescription& b) { return a.name < b.name; });
    return result;
}
bool registerWritable(const RegisterDescription& reg) {
    return reg.access == "RW" || reg.access == "WO" || reg.access == "W1C";
}
}
