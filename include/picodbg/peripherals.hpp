#pragma once
#include "picodbg/chip.hpp"
#include <utility>

namespace picodbg {
struct FieldMeaning {
    uint32_t value;
    std::string text;
};
struct RegisterField {
    std::string name;
    unsigned lsb, width;
    std::string description;
    std::vector<FieldMeaning> meanings;
    RegisterField(std::string fieldName, unsigned bit, unsigned size,
                  std::string detail = {}, std::vector<FieldMeaning> values = {})
        : name(std::move(fieldName)), lsb(bit), width(size),
          description(std::move(detail)), meanings(std::move(values)) {}
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
bool registerWritable(const RegisterDescription& reg);
uint32_t fieldValue(uint32_t value, const RegisterField& field);
std::string fieldMeaning(uint32_t registerValue, const RegisterField& field);
}
