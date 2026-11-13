#pragma once
#include "picodbg/chip.hpp"

namespace picodbg {
// Board wiring is separate from the SoC address map. CYW43 is an external
// device on W boards, not another memory-mapped RP2040/RP2350 peripheral.
enum class Board { Pico, Pico2, PicoW, Pico2W };
struct BoardConnection {
    std::string function, connection, note;
};
struct BoardDescription {
    Board board;
    Chip chip;
    std::string id, name;
    bool wireless;
    uint32_t flashSize;
    std::vector<BoardConnection> connections;
};
BoardDescription describeBoard(Board board);
Board defaultBoard(Chip chip);
bool parseBoard(const std::string& id, Board& board);
}
