#include "picodbg/peripherals.hpp"
#include <algorithm>

namespace picodbg {
uint32_t fieldValue(uint32_t value, const RegisterField& field) {
    if (!field.width || field.width > 32 || field.lsb > 31 || field.width > 32 - field.lsb)
        throw Error(ErrorKind::InvalidArgument, "Champ de registre invalide");
    return field.width == 32 ? value : (value >> field.lsb) & ((1u << field.width) - 1u);
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
        device.registers.push_back({device.base + 0x34, "LOCKED", "RW", "RP2350 : verrou jusqu'au reset, non émulé", {{"LOCKED", 0, 1}}});
        device.registers.push_back({device.base + 0x38, "SOURCE", "RW", "RP2350 : tick ou clk_sys, non émulé", {{"CLK_SYS", 0, 1}}});
    }
    const uint32_t offset = pico2 ? 0x3cu : 0x34u;
    const char* irqNames[] = {"INTR", "INTE", "INTF", "INTS"};
    const char* irqAccess[] = {"W1C", "RW", "RW", "RO"};
    for (unsigned i = 0; i < 4; ++i) {
        device.registers.push_back({device.base + offset + i * 4u, irqNames[i], irqAccess[i], "Interruptions des quatre alarmes", {}});
        for (unsigned n = 0; n < 4; ++n) device.registers.back().fields.push_back({"ALARM" + std::to_string(n), n, 1});
    }
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
            device.registers.push_back({reg.address, reg.name, "Lecture", reg.note, {}});
            if (reg.address == 0xd0000050)
                device.registers.back().fields = {{"VLD", 0, 1}, {"RDY", 1, 1}, {"WOF", 2, 1}, {"ROE", 3, 1}};
            if (reg.address == 0xe000e010)
                device.registers.back().fields = {{"ENABLE", 0, 1}, {"TICKINT", 1, 1}, {"CLKSOURCE", 2, 1}, {"COUNTFLAG", 16, 1}};
        }
        if (!device.registers.empty()) device.coverage = "Partiel";
        if (device.name == "TIMER" || device.name == "TIMER0" || device.name == "TIMER1") {
            device.registers.clear(); timerRegisters(device, chip == Chip::RP2350);
            device.coverage = chip == Chip::RP2040 ? "Partiel (temps virtuel)" : "Non émulé";
        }
        result.push_back(device);
    }
    std::sort(result.begin(), result.end(), [](const PeripheralDescription& a, const PeripheralDescription& b) { return a.name < b.name; });
    return result;
}
}
