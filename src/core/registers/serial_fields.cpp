#include "catalog_details.hpp"

namespace picodbg {
void describeSerialFields(PeripheralDescription& d) {
    if (d.name.find("UART") == 0) {
        describeRegister(d, 0, "UARTDR", "RW", "", {{"DATA", 0, 8, "Octet RX inspecté sans consommation"}, flag("FE", 8, "Erreur de trame"), flag("PE", 9, "Erreur de parité"), flag("BE", 10, "Break reçu"), flag("OE", 11, "Débordement RX")});
        describeRegister(d, 0x18, "UARTFR", "RO", "", {flag("BUSY", 3, "Émission en cours"), flag("RXFE", 4, "FIFO RX vide"), flag("TXFF", 5, "FIFO TX pleine"), flag("RXFF", 6, "FIFO RX pleine"), flag("TXFE", 7, "FIFO TX vide")});
        describeRegister(d, 0x2c, "UARTLCR_H", "RW", "", {
            flag("BRK", 0, "Émettre un break"), flag("PEN", 1, "Parité activée"), flag("EPS", 2, "Parité", "Impaire", "Paire"), flag("STP2", 3, "Deux bits de stop"),
            flag("FEN", 4, "FIFO activées"), {"WLEN", 5, 2, "Taille des données", {{0, "5 bits"}, {1, "6 bits"}, {2, "7 bits"}, {3, "8 bits"}}}, flag("SPS", 7, "Parité forcée")});
        describeRegister(d, 0x30, "UARTCR", "RW", "", {flag("UARTEN", 0, "UART actif"), flag("LBE", 7, "Bouclage TX vers RX"), flag("TXE", 8, "Émetteur autorisé"), flag("RXE", 9, "Récepteur autorisé"), flag("RTSEN", 14, "Contrôle de flux RTS"), flag("CTSEN", 15, "Contrôle de flux CTS")});
        const std::vector<FieldMeaning> thresholds = {{0, "1/8 FIFO"}, {1, "1/4 FIFO"}, {2, "1/2 FIFO"}, {3, "3/4 FIFO"}, {4, "7/8 FIFO"}};
        describeRegister(d, 0x34, "UARTIFLS", "RW", "", {{"TXIFLSEL", 0, 3, "Seuil TX", thresholds}, {"RXIFLSEL", 3, 3, "Seuil RX", thresholds}});
        for (auto& reg : d.registers) if (reg.address >= d.base + 0x38 && reg.address <= d.base + 0x44)
            reg.fields = {flag("RX", 4, "Seuil RX"), flag("TX", 5, "Seuil TX"), flag("RT", 6, "Timeout RX"), flag("FE", 7, "Erreur de trame"), flag("PE", 8, "Erreur de parité"), flag("BE", 9, "Break"), flag("OE", 10, "Débordement RX")};
    } else {
        std::vector<FieldMeaning> widths;
        for (uint32_t i = 3; i < 16; ++i) widths.push_back({i, std::to_string(i + 1) + " bits"});
        describeRegister(d, 0, "SSPCR0", "RW", "", {{"DSS", 0, 4, "Taille d'une trame", widths}, {"FRF", 4, 2, "Format de trame", {{0, "Motorola SPI"}, {1, "TI synchrone"}, {2, "Microwire"}}}, flag("SPO", 6, "Polarité SCLK au repos", "Bas", "Haut"), flag("SPH", 7, "Phase SCLK", "Premier front", "Second front"), {"SCR", 8, 8, "Diviseur série supplémentaire : SCR + 1"}});
        describeRegister(d, 4, "SSPCR1", "RW", "", {flag("LBM", 0, "Bouclage interne"), flag("SSE", 1, "Interface activée"), flag("MS", 2, "Rôle SPI", "Maître", "Esclave"), flag("SOD", 3, "Sortie esclave désactivée")});
        describeRegister(d, 0xc, "SSPSR", "RO", "", {flag("TFE", 0, "FIFO TX vide"), flag("TNF", 1, "FIFO TX non pleine"), flag("RNE", 2, "FIFO RX non vide"), flag("RFF", 3, "FIFO RX pleine"), flag("BSY", 4, "Transfert en cours")});
    }
}
}
