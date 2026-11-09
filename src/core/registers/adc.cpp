#include "catalog_details.hpp"

namespace picodbg {
void describeAdc(Chip chip, PeripheralDescription& d) {
    const bool pico2 = chip == Chip::RP2350;
    describeRegister(d, 0, "CS", "Mixte", "Commande ADC ; champs de statut RO et ERR_STICKY W1C", {
        flag("EN", 0, "Alimentation ADC et horloge"), flag("TS_EN", 1, "Capteur de température"),
        flag("START_ONCE", 2, "Déclenche une conversion ; bit auto-effacé"),
        flag("START_MANY", 3, "Conversions continues"), flag("READY", 8, "Prêt pour une conversion"),
        flag("ERR", 9, "Erreur de la dernière conversion"), flag("ERR_STICKY", 10, "Erreur mémorisée ; écrire 1 pour effacer"),
        {"AINSEL", 12, pico2 ? 4u : 3u, "Entrée analogique sélectionnée ; dépend du boîtier"},
        {"RROBIN", 16, pico2 ? 9u : 5u, "Un bit par entrée du balayage cyclique"}});
    describeRegister(d, 4, "RESULT", "RO", "Dernière conversion", {{"RESULT", 0, 12, "Résultat brut sur 12 bits ; pas une tension sans référence analogique"}});
    describeRegister(d, 8, "FCS", "Mixte", "Configuration et état FIFO ; UNDER/OVER W1C", {
        flag("EN", 0, "Stocker les conversions dans la FIFO"), flag("SHIFT", 1, "Réduire le résultat à 8 bits"),
        flag("ERR", 2, "Inclure le bit d'erreur dans les entrées FIFO"), flag("DREQ_EN", 3, "Requêtes DMA"),
        flag("EMPTY", 8, "FIFO vide"), flag("FULL", 9, "FIFO pleine"),
        flag("UNDER", 10, "Lecture d'une FIFO vide"), flag("OVER", 11, "Conversion perdue : FIFO pleine"),
        {"LEVEL", 16, 4, "Nombre de résultats en attente"}, {"THRESH", 24, 4, "Seuil FIFO pour DREQ/IRQ"}});
    describeRegister(d, 0xc, "FIFO", "RO", "Lecture matérielle dépilante", {{"VAL", 0, 12, "Conversion brute"}, flag("ERR", 15, "Erreur associée à ce résultat")});
    describeRegister(d, 0x10, "DIV", "RW", "Période de conversion : 1 + INT + FRAC/256 cycles ADC", {
        {"FRAC", 0, 8, "Fraction en unités de 1/256"}, {"INT", 8, 16, "Partie entière ; cadence limitée par la durée de conversion"}});
    const char* names[] = {"INTR", "INTE", "INTF", "INTS"};
    for (unsigned i = 0; i < 4; ++i)
        describeRegister(d, 0x14 + i * 4, names[i], i == 0 || i == 3 ? "RO" : "RW", "IRQ FIFO : brut, masque, forçage, état masqué", {flag("FIFO", 0, "Seuil FIFO atteint / interruption autorisée ou forcée selon le registre")});
}
}
