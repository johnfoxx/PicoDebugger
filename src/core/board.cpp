#include "picodbg/board.hpp"

namespace picodbg {
Board defaultBoard(Chip chip) { return chip == Chip::RP2350 ? Board::Pico2 : Board::Pico; }
bool parseBoard(const std::string& id, Board& board) {
    for (const auto candidate : {Board::Pico, Board::PicoW, Board::Pico2, Board::Pico2W}) {
        if (describeBoard(candidate).id == id) { board = candidate; return true; }
    }
    return false;
}
BoardDescription describeBoard(Board board) {
    const bool pico2 = board == Board::Pico2 || board == Board::Pico2W;
    const bool wireless = board == Board::PicoW || board == Board::Pico2W;
    BoardDescription d;
    d.board = board; d.chip = pico2 ? Chip::RP2350 : Chip::RP2040;
    d.id = pico2 ? (wireless ? "pico2_w" : "pico2") : (wireless ? "pico_w" : "pico");
    d.name = pico2 ? (wireless ? "Pico 2 W" : "Pico 2") : (wireless ? "Pico W" : "Pico");
    d.wireless = wireless; d.flashSize = (pico2 ? 4u : 2u) * 1024u * 1024u;
    // Source: Raspberry Pi Pico SDK 2.2.0 board headers; see docs/BOARDS.md.
    d.connections = {
        {"LED intégrée", wireless ? "CYW43 · WL_GPIO0" : "GPIO25", wireless
            ? "Commande via le pilote radio ; état non émulé. GPIO25 est la sélection radio."
            : "Active haut ; latch SIO consultable, pas de signal électrique simulé."},
        {"Mode alimentation SMPS", wireless ? "CYW43 · WL_GPIO1" : "GPIO23", "Câblage décrit ; alimentation non simulée."},
        {"Détection VBUS", wireless ? "CYW43 · WL_GPIO2" : "GPIO24", "Entrée physique non simulée."},
        {"Mesure VSYS", "GPIO29 / ADC3", wireless ? "Partagé avec l'horloge radio ; ADC non émulé." : "ADC non émulé."},
        {"UART0 par défaut", "TX GPIO0 · RX GPIO1", "Choix du SDK ; pinmux non émulé."},
        {"I2C0 par défaut", "SDA GPIO4 · SCL GPIO5", "Registres décrits ; contrôleur non émulé."},
        {"SPI0 par défaut", "RX 16 · CS 17 · SCK 18 · TX 19", "Loopback interne seulement ; pinmux non émulé."}
    };
    if (wireless) {
        d.connections.push_back({"CYW43 · alimentation", "GPIO23 / WL_REG_ON", "Puce radio externe ; non émulée."});
        d.connections.push_back({"CYW43 · données / IRQ", "GPIO24 / WL_DATA / HOST_WAKE", "Liaison SPI partagée, pilotée par le SDK ; PIO non émulé."});
        d.connections.push_back({"CYW43 · sélection", "GPIO25 / WL_CS", "Ce GPIO ne commande pas la LED de la carte W."});
        d.connections.push_back({"CYW43 · horloge", "GPIO29 / WL_CLOCK", "Partagé avec VSYS ; aucun registre CYW43 dans la carte MMIO du SoC."});
    }
    return d;
}
}
