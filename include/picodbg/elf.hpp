#pragma once
#include "picodbg/memory.hpp"
#include <map>

namespace picodbg {
struct Segment {
    uint32_t address;
    std::vector<uint8_t> bytes;
};
struct ElfImage {
    uint32_t entry = 0;
    uint32_t vectorAddress = 0;
    bool hasVectors = false;
    std::vector<Segment> segments;
    std::map<std::string, uint32_t> symbols;
    std::map<uint32_t, std::string> functions;
    std::map<uint32_t, uint32_t> functionSizes;
    static ElfImage read(const std::string& path);
    static ElfImage parse(const std::vector<uint8_t>& bytes);
    void install(Memory& memory) const;
    std::string symbolAt(uint32_t address) const;
    std::string functionAt(uint32_t address) const;
};
}
