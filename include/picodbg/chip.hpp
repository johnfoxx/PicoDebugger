#pragma once
#include "picodbg/types.hpp"
#include <vector>

namespace picodbg {
struct Region {
    uint32_t begin;
    uint64_t end; // Exclusive; 64 bits avoids wrap at the end of the address space.
    std::string name;
    std::string permissions;
    std::string coverage;
};
struct ChipDescription {
    Chip chip;
    std::string name;
    uint32_t ramSize;
    uint32_t flashSize;
    std::vector<Region> regions;
};
ChipDescription describe(Chip chip);
const Region* regionAt(const ChipDescription& chip, uint32_t address);
inline bool regionWritable(const Region& region) {
    return region.permissions.compare(0, 3, "R/W") == 0;
}
struct PeripheralRegister {
    uint32_t address;
    std::string name;
    std::string note;
};
std::vector<PeripheralRegister> inspectableRegisters(Chip chip);
}
