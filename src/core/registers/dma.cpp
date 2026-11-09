#include "catalog_details.hpp"

namespace picodbg {
void describeDma(Chip chip, PeripheralDescription& d) {
    const bool pico2 = chip == Chip::RP2350;
    const unsigned shift = pico2 ? 2u : 0u;
    std::vector<RegisterField> control = {
        flag("EN", 0, "Autoriser le canal ; désactiver met en pause"), flag("HIGH_PRIORITY", 1, "Priorité d'arbitrage DMA"),
        {"DATA_SIZE", 2, 2, "Taille d'un transfert", {{0, "8 bits"}, {1, "16 bits"}, {2, "32 bits"}}},
        flag("INCR_READ", 4, "Incrément de l'adresse source"), flag("INCR_WRITE", pico2 ? 6u : 5u, "Incrément de l'adresse destination"),
        {"RING_SIZE", 6 + shift, 4, "0 : pas de boucle ; sinon bouclage sur les n bits bas"},
        flag("RING_SEL", 10 + shift, "Adresse circulaire", "Lecture", "Écriture"),
        {"CHAIN_TO", 11 + shift, 4, "Canal déclenché en fin de transfert ; soi-même désactive le chaînage"},
        {"TREQ_SEL", 15 + shift, 6, "Source DREQ de cadencement", {{59, "Timer DMA 0"}, {60, "Timer DMA 1"}, {61, "Timer DMA 2"}, {62, "Timer DMA 3"}, {63, "Permanent (sans DREQ)"}}},
        flag("IRQ_QUIET", 21 + shift, "Fin de bloc sans IRQ automatique"), flag("BSWAP", 22 + shift, "Permutation des octets"),
        flag("SNIFF_EN", 23 + shift, "Autoriser le calculateur de checksum"), flag("BUSY", 24 + shift, "Séquence en cours"),
        flag("WRITE_ERROR", 29, "Erreur bus d'écriture ; W1C"), flag("READ_ERROR", 30, "Erreur bus de lecture ; W1C"), flag("AHB_ERROR", 31, "Au moins une erreur bus")};
    if (pico2) {
        control.push_back(flag("INCR_READ_REV", 5, "Modifie le sens/pas source ; dépend de INCR_READ"));
        control.push_back(flag("INCR_WRITE_REV", 7, "Modifie le sens/pas destination ; dépend de INCR_WRITE"));
    }
    const std::vector<RegisterField> count = pico2
        ? std::vector<RegisterField>{{"COUNT", 0, 28, "Nombre de transferts, pas d'octets"}, {"MODE", 28, 4, "Mode de décompte RP2350 ; voir le manuel DMA"}}
        : std::vector<RegisterField>{{"COUNT", 0, 32, "Nombre de transferts, pas d'octets"}};
    for (unsigned ch = 0; ch < (pico2 ? 16u : 12u); ++ch) {
        const uint32_t base = ch * 0x40;
        const auto name = "CH" + std::to_string(ch) + "_";
        describeRegister(d, base, name + "READ_ADDR", "RW", "Adresse source du canal", {{"ADDRESS", 0, 32, "Adresse courante de lecture"}});
        describeRegister(d, base + 4, name + "WRITE_ADDR", "RW", "Adresse destination du canal", {{"ADDRESS", 0, 32, "Adresse courante d'écriture"}});
        describeRegister(d, base + 8, name + "TRANS_COUNT", "RW", "Compteur de transferts restants / valeur de rechargement", count);
        describeRegister(d, base + 12, name + "CTRL_TRIG", "Mixte", "Contrôle et statut ; écrire peut déclencher le canal ; erreurs W1C", control);
        // Alias grouped by the hardware's trigger layout, not atomic SET/CLR.
        const char* aliases[] = {"AL1_CTRL", "AL1_READ_ADDR", "AL1_WRITE_ADDR", "AL1_TRANS_COUNT_TRIG",
            "AL2_CTRL", "AL2_TRANS_COUNT", "AL2_READ_ADDR", "AL2_WRITE_ADDR_TRIG",
            "AL3_CTRL", "AL3_WRITE_ADDR", "AL3_TRANS_COUNT", "AL3_READ_ADDR_TRIG"};
        for (unsigned i = 0; i < 12; ++i) {
            const std::string alias(aliases[i]);
            describeRegister(d, base + 0x10 + i * 4, name + alias, "RW", "Alias DMA ; suffixe TRIG : écriture déclenchante",
                i % 4 == 0 ? control : alias.find("TRANS_COUNT") != std::string::npos ? count : std::vector<RegisterField>{{"ADDRESS", 0, 32, "Adresse de transfert"}});
        }
    }
}
}
