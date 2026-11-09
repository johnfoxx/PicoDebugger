#pragma once
#include "picodbg/peripherals.hpp"

namespace picodbg {
// Static hardware documentation only. These functions never install a device
// model, change its coverage label, or supply invented live/reset values.
void describePeripheralRegisters(Chip chip, PeripheralDescription& device);
void describeAdc(Chip chip, PeripheralDescription& device);
void describeI2c(PeripheralDescription& device);
void describeDma(Chip chip, PeripheralDescription& device);
void describePio(Chip chip, PeripheralDescription& device);
void describeSerialFields(PeripheralDescription& device);

inline RegisterDescription& describeRegister(PeripheralDescription& device, uint32_t offset,
                                            const std::string& name, const std::string& access,
                                            const std::string& note,
                                            std::vector<RegisterField> fields = {}) {
    for (auto& reg : device.registers) if (reg.address == device.base + offset) {
        // Existing notes describe emulator limits and must remain visible.
        reg.fields = std::move(fields);
        return reg;
    }
    device.registers.push_back({device.base + offset, name, access, note, std::move(fields)});
    return device.registers.back();
}
inline RegisterField flag(const std::string& name, unsigned bit, const std::string& note,
                          const std::string& zero = "Non", const std::string& one = "Oui") {
    return {name, bit, 1, note, {{0, zero}, {1, one}}};
}
}
