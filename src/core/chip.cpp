#include "picodbg/chip.hpp"
#include <algorithm>

namespace picodbg {
ChipDescription describe(Chip chip) {
    ChipDescription d;
    d.chip = chip;
    const bool pico2 = chip == Chip::RP2350;
    d.name = pico2 ? "RP2350 / Cortex-M33 (sous-ensemble expérimental)" : "RP2040 / Cortex-M0+ (modèle fonctionnel partiel)";
    d.ramSize = pico2 ? 520u * 1024u : 264u * 1024u;
    d.flashSize = pico2 ? 4u * 1024u * 1024u : 2u * 1024u * 1024u;
    d.regions = {
        {0, pico2 ? 0x8000u : 0x4000u, "Boot ROM", "R/X", "Non émulée : démarrage direct de l'ELF"},
        {0x10000000, 0x10000000ull + d.flashSize, "Flash XIP (capacité Pico standard)", "R/X", "Octets ELF ; pas de cache ni de programmation flash"},
        {0x20000000, 0x20000000ull + (pico2 ? 0x80000u : 0x40000u), "SRAM partagée / banques entrelacées", "R/W/X", "Mémoire fonctionnelle, sans contention"},
        {pico2 ? 0x20080000u : 0x20040000u, pico2 ? 0x20081000ull : 0x20041000ull, "SRAM scratch X", "R/W/X", "Partagée entre les deux cœurs"},
        {pico2 ? 0x20081000u : 0x20041000u, pico2 ? 0x20082000ull : 0x20042000ull, "SRAM scratch Y", "R/W/X", "Partagée entre les deux cœurs"}
    };
    if (!pico2) {
        d.regions.push_back({0x21000000, 0x21040000ull, "Alias SRAM non entrelacé", "R/W/X", "Banques 0–3, alias physique traduit"});
        const char* names[] = {"SYSINFO", "SYSCFG", "CLOCKS", "RESETS", "PSM", "IO_BANK0", "IO_QSPI", "PADS_BANK0", "PADS_QSPI", "XOSC", "PLL_SYS", "PLL_USB", "BUSCTRL", "UART0", "UART1", "SPI0", "SPI1", "I2C0", "I2C1", "ADC", "PWM", "TIMER", "WATCHDOG", "RTC", "ROSC", "VREG_AND_CHIP_RESET"};
        for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
            d.regions.push_back({0x40000000u + i * 0x4000u, 0x40000000ull + (i + 1) * 0x4000u, names[i], "MMIO", "Cartographié, non émulé (inclut fenêtres d'alias)"});
        d.regions.push_back({0x4006c000, 0x40070000ull, "TBMAN", "MMIO", "Non émulé"});
        d.regions.push_back({0x14000000, 0x14004000ull, "XIP_CTRL", "MMIO", "Non émulé"});
        d.regions.push_back({0x15000000, 0x15004000ull, "XIP SRAM/cache", "R/W", "Non émulé"});
        d.regions.push_back({0x18000000, 0x18004000ull, "XIP_SSI", "MMIO", "Non émulé"});
    } else {
        const char* names[] = {"SYSINFO", "SYSCFG", "CLOCKS", "PSM", "RESETS", "IO_BANK0", "IO_QSPI", "PADS_BANK0", "PADS_QSPI", "XOSC", "PLL_SYS", "PLL_USB", "ACCESSCTRL", "BUSCTRL", "UART0", "UART1", "SPI0", "SPI1", "I2C0", "I2C1", "ADC", "PWM", "TIMER0", "TIMER1", "HSTX_CTRL", "XIP_CTRL", "XIP_QMI", "WATCHDOG", "BOOTRAM", "ROSC", "TRNG", "SHA256", "POWMAN", "TICKS"};
        for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
            d.regions.push_back({0x40000000u + i * 0x8000u, 0x40000000ull + (i + 1) * 0x8000u, names[i], "MMIO", "Cartographié, non émulé (fenêtre de décodage)"});
        d.regions.push_back({0x40120000, 0x40140000ull, "OTP / données / alias", "MMIO", "Non émulé"});
        d.regions.push_back({0x40140000, 0x40150000ull, "CoreSight", "MMIO", "Non émulé"});
        d.regions.push_back({0x40150000, 0x40168000ull, "DFT / GLITCH_DETECTOR / TBMAN", "MMIO", "Non émulé"});
    }
    d.regions.push_back({0x50000000, 0x50010000ull, "DMA", "MMIO", "Non émulé"});
    d.regions.push_back({0x50100000, 0x50101000ull, "USB DPRAM", "R/W", "Non émulé"});
    d.regions.push_back({0x50110000, 0x50114000ull, "USB contrôleur", "MMIO", "Non émulé"});
    d.regions.push_back({0x50200000, 0x50204000ull, "PIO0", "MMIO", "Non émulé"});
    d.regions.push_back({0x50300000, 0x50304000ull, "PIO1", "MMIO", "Non émulé"});
    if (pico2) d.regions.push_back({0x50400000, 0x50404000ull, "PIO2", "MMIO", "Non émulé"});
    d.regions.push_back({pico2 ? 0x50500000u : 0x50400000u, pico2 ? 0x50504000ull : 0x50404000ull, "XIP_AUX", "MMIO", "Non émulé"});
    if (pico2) d.regions.push_back({0x50600000, 0x50701000ull, "HSTX FIFO / trace CoreSight", "MMIO", "Non émulé"});
    d.regions.push_back({0xd0000000, 0xd0001000ull, "SIO / accès propre au cœur", "MMIO", "CPUID, FIFO, spinlocks ; GPIO sortie RP2040"});
    d.regions.push_back({0xe0000000, 0xe0100000ull, "PPB / NVIC / SysTick / SCB (par cœur)", "MMIO", "Sous-ensemble ; RP2350 : non émulé"});
    std::sort(d.regions.begin(), d.regions.end(), [](const Region& a, const Region& b) { return a.begin < b.begin; });
    return d;
}
const Region* regionAt(const ChipDescription& chip, uint32_t address) {
    for (const auto& region : chip.regions)
        if (address >= region.begin && address < region.end) return &region;
    return nullptr;
}
std::vector<PeripheralRegister> inspectableRegisters(Chip chip) {
    std::vector<PeripheralRegister> registers = {
        {0xd0000000, "SIO.CPUID", "Identifiant du cœur sélectionné"},
        {0xd0000050, "SIO.FIFO_ST", "VLD bit0, RDY bit1, WOF bit2, ROE bit3"},
        {0xd0000058, "SIO.FIFO_RD", "Inspection sans dépiler ; zéro si FIFO vide"}
    };
    for (unsigned i = 0; i < 32; ++i) registers.push_back({0xd0000100u + i * 4u, "SIO.SPINLOCK" + std::to_string(i), "Inspection sans acquérir ; 0 = verrou pris"});
    if (chip == Chip::RP2040) {
        registers.push_back({0xd0000010, "SIO.GPIO_OUT", "Latch de sortie uniquement ; aucune broche externe simulée"});
        registers.push_back({0xd0000020, "SIO.GPIO_OE", "Output enable"});
        registers.push_back({0xe000e010, "SysTick.CSR", "Inspection sans effacer COUNTFLAG ; temps virtuel"});
        registers.push_back({0xe000e014, "SysTick.RVR", "Valeur de rechargement 24 bits"});
        registers.push_back({0xe000e018, "SysTick.CVR", "Compteur virtuel ; pas de cycles matériels"});
        registers.push_back({0xe000e100, "NVIC.ISER", "Interruptions activées (priorités fixes uniquement)"});
        registers.push_back({0xe000e200, "NVIC.ISPR", "Interruptions en attente"});
        registers.push_back({0xe000ed08, "SCB.VTOR", "Table des vecteurs du contexte sélectionné"});
    }
    return registers;
}
}
