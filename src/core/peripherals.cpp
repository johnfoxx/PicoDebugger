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
    for (const unsigned index : {0u, 2u, 9u})
        device.registers[index].fields = {{"TIME_HI", 0, 32, "Bits 63 à 32 du compteur"}};
    for (const unsigned index : {1u, 3u, 10u})
        device.registers[index].fields = {{"TIME_LO", 0, 32, "Bits 31 à 0 du compteur"}};
    for (unsigned i = 0; i < 4; ++i) {
        device.registers[4 + i].fields = {{"ALARM", 0, 32, "Déclenchement à cette valeur de la partie basse du compteur"}};
        device.registers[8].fields.push_back(flag("ALARM" + std::to_string(i), i, "État de l’alarme", "Désarmée", "Armée"));
    }
    device.registers[11].fields = {flag("DBG0", 1, "Pause lorsque le cœur 0 est en debug"), flag("DBG1", 2, "Pause lorsque le cœur 1 est en debug")};
    device.registers[12].fields = {flag("PAUSE", 0, "Comptage", "En cours", "En pause")};
    if (pico2) {
        device.registers.push_back({device.base + 0x34, "LOCKED", "RW", "Verrouille les écritures jusqu'au reset du périphérique",
            {flag("LOCKED", 0, "Écriture des registres", "Autorisée", "Verrouillée")}});
        device.registers.push_back({device.base + 0x38, "SOURCE", "RW", "Deux sources virtuelles, arbre d'horloge absent",
            {flag("CLK_SYS", 0, "Source du compteur", "Tick", "clk_sys")}});
    }
    const uint32_t offset = pico2 ? 0x3cu : 0x34u;
    const char* irqNames[] = {"INTR", "INTE", "INTF", "INTS"};
    const char* irqAccess[] = {"W1C", "RW", "RW", "RO"};
    const char* irqNotes[] = {"État brut de l’IRQ", "Autorisation de l’IRQ", "Forçage logiciel de l’IRQ", "État de l’IRQ après masque et forçage"};
    const char* zero[] = {"Inactive", "Masquée", "Non forcée", "Inactive"};
    const char* one[] = {"Déclenchée", "Autorisée", "Forcée", "Active"};
    for (unsigned i = 0; i < 4; ++i) {
        device.registers.push_back({device.base + offset + i * 4u, irqNames[i], irqAccess[i], "Interruptions des quatre alarmes", {}});
        for (unsigned n = 0; n < 4; ++n)
            device.registers.back().fields.push_back(flag("ALARM" + std::to_string(n), n, irqNotes[i], zero[i], one[i]));
    }
}
void add(PeripheralDescription& device, uint32_t offset, const char* name, const char* access, const char* note) {
    device.registers.push_back({device.base + offset, name, access, note, {}});
}
std::vector<RegisterField> gpioFields(unsigned count, unsigned firstGpio, bool enable) {
    std::vector<RegisterField> fields;
    const auto append = [&fields, enable](const std::string& name, unsigned bit) {
        fields.push_back(enable ? flag(name, bit, "Commande de la sortie", "Désactivée", "Activée")
                                : flag(name, bit, "Niveau de sortie SIO mémorisé", "Bas", "Haut"));
    };
    for (unsigned bit = 0; bit < count; ++bit)
        append("GPIO" + std::to_string(firstGpio + bit), bit);
    if (firstGpio == 32) {
        const char* names[] = {"USB_DP", "USB_DM", "QSPI_SCK", "QSPI_CSN", "QSPI_SD0", "QSPI_SD1", "QSPI_SD2", "QSPI_SD3"};
        for (unsigned bit = 0; bit < 8; ++bit) append(names[bit], 24 + bit);
    }
    return fields;
}
void sioFields(PeripheralDescription& device, bool pico2) {
    for (auto& reg : device.registers) {
        const auto offset = reg.address - device.base;
        switch (offset) {
        case 0: reg.fields = {{"CPUID", 0, 32, "Identifiant du cœur", {{0, "Cœur 0"}, {1, "Cœur 1"}}}}; break;
        case 0x10: reg.fields = gpioFields(pico2 ? 32 : 30, 0, false); break;
        case 0x14: reg.fields = gpioFields(16, 32, false); break;
        case 0x20: case 0x30: reg.fields = gpioFields(pico2 ? 32 : 30, 0, true); break;
        case 0x34: reg.fields = gpioFields(16, 32, true); break;
        case 0x50:
            reg.fields = {flag("VLD", 0, "Donnée à lire"), flag("RDY", 1, "Place pour écrire"),
                flag("WOF", 2, "Écriture tentée dans une FIFO pleine"), flag("ROE", 3, "Lecture tentée dans une FIFO vide")};
            reg.fields[0].access = reg.fields[1].access = "RO";
            break;
        case 0x54: case 0x58:
            reg.fields = {{"DATA", 0, 32, offset == 0x54 ? "Mot transmis à l’autre cœur" : "Mot reçu de l’autre cœur"}};
            break;
        default:
            if (offset >= 0x100 && offset < 0x180) {
                const unsigned lock = (offset - 0x100) / 4;
                reg.fields = {flag("LOCK" + std::to_string(lock), lock, "État du verrou en inspection", "Pris", "Libre")};
            }
        }
    }
}
void systemFields(RegisterDescription& reg, bool pico2) {
    switch (reg.address) {
    case 0xe000e010:
        reg.fields = {flag("ENABLE", 0, "Compteur SysTick", "Arrêté", "Actif"), flag("TICKINT", 1, "Exception SysTick", "Désactivée", "Activée"),
            flag("CLKSOURCE", 2, "Source d’horloge", "Externe", "Horloge CPU"), flag("COUNTFLAG", 16, "Le compteur a atteint zéro")};
        break;
    case 0xe000e014: reg.fields = {{"RELOAD", 0, 24, "Valeur de rechargement du compteur"}}; break;
    case 0xe000e018: reg.fields = {{"CURRENT", 0, 24, "Valeur courante du compteur"}}; break;
    case 0xe000ed08: {
        const unsigned alignment = pico2 ? 7 : 8;
        reg.fields = {{"TBLOFF", alignment, 32 - alignment, "Bits hauts de l’adresse de la table vectorielle"}};
        break;
    }
    case 0xe000e100: case 0xe000e104: case 0xe000e200: case 0xe000e204: {
        const bool highBank = (reg.address & 4u) != 0;
        const bool pending = (reg.address & 0x200u) != 0;
        for (unsigned bit = 0; bit < (highBank ? 20u : 32u); ++bit) {
            const auto name = "IRQ" + std::to_string(bit + (highBank ? 32u : 0u));
            reg.fields.push_back(pending ? flag(name, bit, "État de l’interruption", "Inactive", "En attente")
                                         : flag(name, bit, "Autorisation de l’interruption", "Désactivée", "Activée"));
        }
        break;
    }
    }
}
void resetFields(PeripheralDescription& device, bool pico2) {
    const char* rp2040[] = {"ADC", "BUSCTRL", "DMA", "I2C0", "I2C1", "IO_BANK0", "IO_QSPI", "JTAG",
        "PADS_BANK0", "PADS_QSPI", "PIO0", "PIO1", "PLL_SYS", "PLL_USB", "PWM", "RTC", "SPI0", "SPI1", "SYSCFG",
        "SYSINFO", "TBMAN", "TIMER", "UART0", "UART1", "USBCTRL"};
    const char* rp2350[] = {"ADC", "BUSCTRL", "DMA", "HSTX", "I2C0", "I2C1", "IO_BANK0", "IO_QSPI", "JTAG",
        "PADS_BANK0", "PADS_QSPI", "PIO0", "PIO1", "PIO2", "PLL_SYS", "PLL_USB", "PWM", "SHA256", "SPI0", "SPI1",
        "SYSCFG", "SYSINFO", "TBMAN", "TIMER0", "TIMER1", "TRNG", "UART0", "UART1", "USBCTRL"};
    const auto* names = pico2 ? rp2350 : rp2040;
    const auto count = pico2 ? sizeof(rp2350) / sizeof(*rp2350) : sizeof(rp2040) / sizeof(*rp2040);
    for (unsigned bit = 0; bit < count; ++bit) {
        const auto* name = names[bit];
        device.registers[0].fields.push_back(flag(name, bit, "Ligne de reset", "Relâchée", "Maintenue"));
        device.registers[1].fields.push_back(flag(name, bit, "Reset par le watchdog", "Exclu", "Sélectionné"));
        device.registers[2].fields.push_back(flag(name, bit, "Sortie de reset", "En attente", "Terminée"));
    }
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
        auto& fields = device.registers.back().fields;
        switch (j) {
        case 0: fields = {flag("EN", 0, "Compteur actif"), flag("PH_CORRECT", 1, "Mode de comptage", "Montant", "Montant puis descendant"),
            flag("A_INV", 2, "Sortie A inversée"), flag("B_INV", 3, "Sortie B inversée")}; break;
        case 1: fields = {{"FRAC", 0, 4, "Partie fractionnaire du diviseur, en seizièmes"}, {"INT", 4, 8, "Partie entière du diviseur"}}; break;
        case 2: fields = {{"CTR", 0, 16, "Valeur courante du compteur"}}; break;
        case 3: fields = {{"A", 0, 16, "Seuil de comparaison du canal A"}, {"B", 16, 16, "Seuil de comparaison du canal B"}}; break;
        case 4: fields = {{"TOP", 0, 16, "Valeur maximale du compteur"}}; break;
        }
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
            if (region.begin == 0xe0000000) systemFields(device.registers.back(), chip == Chip::RP2350);
        }
        if (!device.registers.empty()) device.coverage = "Partiel";
        if (device.name == "SIO") {
            add(device, 0x54, "SIO.FIFO_WR", "WO", "Envoie un mot dans la FIFO du cœur opposé");
            sioFields(device, chip == Chip::RP2350);
            const bool pico2 = chip == Chip::RP2350;
            for (unsigned bank = 0; bank < (pico2 ? 2u : 1u); ++bank) {
                const auto prefix = bank ? std::string("SIO.GPIO_HI_") : std::string("SIO.GPIO_");
                std::vector<RegisterField> inputs;
                for (unsigned bit = 0; bit < (bank ? 16u : pico2 ? 32u : 30u); ++bit)
                    inputs.push_back(flag("GPIO" + std::to_string(bank * 32u + bit), bit, "Niveau d’entrée", "Bas", "Haut"));
                device.registers.emplace_back(device.base + 4u + 4u * bank, prefix + "IN", "RO", "Entrées numériques", std::move(inputs));
                const unsigned step = pico2 ? 8u : 4u;
                const char* operations[] = {"SET", "CLR", "XOR"};
                for (unsigned side = 0; side < 2; ++side)
                    for (unsigned operation = 0; operation < 3; ++operation) {
                        const auto offset = 0x10u + (side * 4u + operation + 1u) * step + 4u * bank;
                        device.registers.emplace_back(device.base + offset, prefix + (side ? "OE_" : "OUT_") + operations[operation],
                            "WO", "Écriture atomique du masque GPIO", gpioFields(bank ? 16u : pico2 ? 32u : 30u, 32u * bank, side != 0));
                    }
            }
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
        if (device.name == "IO_BANK0") {
            const bool pico2 = chip == Chip::RP2350;
            const std::vector<FieldMeaning> overrides = {{0, "Normal"}, {1, "Inversé"}, {2, "Bas"}, {3, "Haut"}};
            const std::vector<RegisterField> control = {{"FUNCSEL", 0, 5, "Fonction de la broche", {{5, "SIO"}, {31, "NULL"}}},
                {"OUTOVER", pico2 ? 12u : 8u, 2, "Forçage du niveau", {{0, "Normal"}, {1, "Inversé"}, {2, "Bas"}, {3, "Haut"}}},
                {"OEOVER", pico2 ? 14u : 12u, 2, "Forçage de l’activation", {{0, "Normale"}, {1, "Inversée"}, {2, "Désactivée"}, {3, "Activée"}}},
                {"INOVER", 16, 2, "Forçage du niveau d’entrée", overrides}, {"IRQOVER", 28, 2, "Forçage du signal d’interruption", overrides}};
            std::vector<RegisterField> status = {{"OUTTOPAD", 9, 1, "Niveau envoyé au pad"},
                {"OETOPAD", 13, 1, "Activation de sortie envoyée au pad"}, {"INFROMPAD", 17, 1, "Niveau lu sur le pad"},
                {"IRQTOPROC", 26, 1, "Signal d’interruption envoyé au CPU"}};
            if (!pico2) {
                status.emplace_back("OUTFROMPERI", 8, 1, "Niveau fourni par le périphérique");
                status.emplace_back("OEFROMPERI", 12, 1, "Activation de sortie fournie par le périphérique");
                status.emplace_back("INTOPERI", 19, 1, "Niveau transmis au périphérique");
                status.emplace_back("IRQFROMPAD", 24, 1, "Signal d’interruption reçu du pad");
            }
            for (unsigned pin = 0; pin < gpioCount(chip); ++pin) {
                const auto name = "GPIO" + std::to_string(pin);
                device.registers.emplace_back(device.base + 8u * pin, name + "_STATUS", "RO", "État de la broche", status);
                device.registers.emplace_back(device.base + 8u * pin + 4u, name + "_CTRL", "RW", "Fonction et forçages de la broche", control);
            }
            const char* irqNames[] = {"INTR", "PROC0_INTE", "PROC0_INTF", "PROC0_INTS", "PROC1_INTE", "PROC1_INTF",
                "PROC1_INTS", "DORMANT_WAKE_INTE", "DORMANT_WAKE_INTF", "DORMANT_WAKE_INTS"};
            const char* bitNames[] = {"LEVEL_LOW", "LEVEL_HIGH", "EDGE_LOW", "EDGE_HIGH"};
            const char* bitNotes[] = {"Niveau bas", "Niveau haut", "Front descendant", "Front montant"};
            const unsigned words = gpioIrqWords(chip);
            for (unsigned group = 0; group < 10; ++group) for (unsigned word = 0; word < words; ++word) {
                const auto offset = gpioIrqOffset(chip) + 4u * (group * words + word);
                const bool statusReg = group % 3 == 0;
                const auto* access = group == 0 ? "W1C" : statusReg ? "RO" : "RW";
                const auto* note = group == 0 ? "Interruptions brutes et acquittement des fronts" :
                    statusReg ? "État après masque et forçage" : group % 3 == 1 ? "Autorisation des événements GPIO" : "Forçage des événements GPIO";
                std::vector<RegisterField> bits;
                for (unsigned pin = 0; pin < 8 && word * 8 + pin < gpioCount(chip); ++pin) {
                    const auto prefix = "GPIO" + std::to_string(word * 8 + pin) + "_";
                    for (unsigned bit = 0; bit < 4; ++bit) {
                        auto field = flag(prefix + bitNames[bit], pin * 4 + bit, bitNotes[bit]);
                        if (statusReg && (group || bit < 2)) field.access = "RO";
                        bits.push_back(std::move(field));
                    }
                }
                device.registers.emplace_back(device.base + offset, std::string(irqNames[group]) + std::to_string(word), access, note, std::move(bits));
            }
            device.coverage = "Partiel (configuration GPIO)";
        }
        if (device.name == "PADS_BANK0") {
            std::vector<RegisterField> fields = {flag("OD", 7, "Sortie du pad", "Autorisée", "Désactivée"), flag("IE", 6, "Entrée du pad", "Désactivée", "Activée"),
                {"DRIVE", 4, 2, "Courant de commande", {{0, "2 mA"}, {1, "4 mA"}, {2, "8 mA"}, {3, "12 mA"}}},
                flag("PUE", 3, "Résistance de rappel au niveau haut"), flag("PDE", 2, "Résistance de rappel au niveau bas"),
                flag("SCHMITT", 1, "Hystérésis d’entrée"), flag("SLEWFAST", 0, "Vitesse de transition", "Lente", "Rapide")};
            if (chip == Chip::RP2350) fields.push_back(flag("ISO", 8, "Isolation du pad", "Connecté", "Isolé"));
            device.registers.emplace_back(device.base, "VOLTAGE_SELECT", "RW", "Tension du banc GPIO",
                std::vector<RegisterField>{flag("VOLTAGE_SELECT", 0, "Tension nominale des pads", "3,3 V", "1,8 V")});
            for (unsigned pin = 0; pin < gpioCount(chip); ++pin)
                device.registers.emplace_back(device.base + 4u + 4u * pin, "GPIO" + std::to_string(pin), "RW", "Configuration du pad", fields);
            device.coverage = "Partiel (pads GPIO)";
        }
        if (device.name == "RESETS") {
            add(device, 0, "RESET", "RW", "Chaque ligne vaut 1 au démarrage. La mettre à 0 libère le bloc correspondant.");
            add(device, 4, "WDSEL", "RW", "Sélection stockée ; watchdog non exécuté");
            add(device, 8, "RESET_DONE", "RO", "Complétion immédiate du reset dans le modèle");
            resetFields(device, chip == Chip::RP2350);
            device.coverage = "Partiel (modèles présents)";
        }
        if (device.name == "WATCHDOG") {
            for (unsigned i = 0; i < 8; ++i)
                describeRegister(device, 0xc + 4u * i, "SCRATCH" + std::to_string(i), "RW", "Registre scratch",
                    {{"DATA", 0, 32, "Mot de stockage logiciel"}});
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
FieldWriteMode fieldWriteMode(const RegisterDescription& reg, const RegisterField& field) {
    if (reg.documentedOnly || !registerWritable(reg) || field.name == "—") return FieldWriteMode::Unavailable;
    const auto& access = field.access;
    if (reg.access == "W1C")
        return access.empty() || access == "RW" || access == "W1C" ? FieldWriteMode::Clear : FieldWriteMode::Unavailable;
    if (std::any_of(reg.fields.begin(), reg.fields.end(), [](const RegisterField& f) { return f.access == "SPECIAL"; }))
        return FieldWriteMode::Unavailable;
    if (reg.access == "RW" && access == "W1C") return FieldWriteMode::Clear;
    if (reg.access != "RW" || (!field.access.empty() && field.access != "RW")) return FieldWriteMode::Unavailable;
    const auto at = reg.address;
    if (at == 0xe000e018 || (at >= 0xd0000100 && at < 0xd0000180) ||
        at == 0xe000e100 || at == 0xe000e104 || at == 0xe000e200 || at == 0xe000e204 ||
        reg.name == "UARTDR" || reg.name == "SSPDR" || reg.name == "UARTRSR/ECR" ||
        (at == 0xe000e010 && field.lsb == 16))
        return FieldWriteMode::Unavailable;
    return FieldWriteMode::Value;
}
uint32_t fieldWriteValue(const RegisterDescription& reg, const RegisterField& field, uint32_t oldValue, uint32_t newValue) {
    const auto mode = fieldWriteMode(reg, field);
    const auto maximum = fieldValue(0xffffffffu, field);
    if (mode == FieldWriteMode::Unavailable)
        throw Error(ErrorKind::Unsupported, "Ce champ ne permet pas une modification individuelle", reg.address);
    if (newValue > maximum)
        throw Error(ErrorKind::InvalidArgument, "Valeur hors de la plage du champ", reg.address);
    if (reg.access == "W1C") return newValue << field.lsb;
    uint32_t mask = maximum << field.lsb;
    for (const auto& other : reg.fields) {
        if (other.access == "W1C" || other.access == "W1S" || other.access == "W1T" || other.access == "WO")
            mask |= fieldValue(0xffffffffu, other) << other.lsb;
    }
    return (oldValue & ~mask) | (newValue << field.lsb);
}
}
