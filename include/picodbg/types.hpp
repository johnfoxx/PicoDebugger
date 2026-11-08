#pragma once
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace picodbg {
inline std::string hex(uint32_t value, unsigned digits = 8) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(static_cast<int>(digits)) << value;
    return out.str();
}

// Unsupported models and architectural faults must never be confused: the former
// is a limitation of this emulator, the latter belongs to the emulated program.
enum class ErrorKind { BusFault, Alignment, Unsupported, InvalidImage, InvalidArgument };
class Error : public std::runtime_error {
public:
    ErrorKind kind;
    uint32_t address;
    Error(ErrorKind type, const std::string& message, uint32_t at = 0)
        : std::runtime_error(message), kind(type), address(at) {}
};

enum class Chip { RP2040, RP2350 };
enum class Access { Read, Write, Execute };
struct MemoryAccess {
    uint32_t address;
    unsigned size;
    Access kind;
    unsigned core;
};
}
