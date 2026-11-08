#pragma once
#include "picodbg/chip.hpp"

namespace picodbg {
struct RegisterField {
    std::string name;
    unsigned lsb, width;
};
struct RegisterDescription {
    uint32_t address;
    std::string name, access, note;
    std::vector<RegisterField> fields;
};
struct PeripheralDescription {
    uint32_t base;
    std::string name, coverage, note;
    std::vector<RegisterDescription> registers;
};
// Hardware inventory and live state are separate: being listed never means a
// device is implemented, and the catalog never issues reads against a CPU bus.
std::vector<PeripheralDescription> peripheralCatalog(Chip chip);
uint32_t fieldValue(uint32_t value, const RegisterField& field);
}
