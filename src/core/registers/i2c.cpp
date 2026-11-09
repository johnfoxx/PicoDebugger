#include "catalog_details.hpp"

namespace picodbg {
void describeI2c(PeripheralDescription& d) {
    describeRegister(d, 0, "IC_CON", "Mixte", "Configuration DW_apb_i2c ; écrire quand le contrôleur est désactivé", {
        flag("MASTER_MODE", 0, "Autoriser le mode maître"),
        {"SPEED", 1, 2, "Vitesse du bus", {{1, "Standard (100 kbit/s)"}, {2, "Fast / Fast Plus"}}},
        flag("IC_10BITADDR_SLAVE", 3, "Adresse esclave sur 10 bits"), flag("IC_10BITADDR_MASTER", 4, "Adresse maître sur 10 bits"),
        flag("IC_RESTART_EN", 5, "Autoriser RESTART"), flag("IC_SLAVE_DISABLE", 6, "Désactiver le mode esclave"),
        flag("STOP_DET_IFADDRESSED", 7, "STOP uniquement si adressé, en mode esclave"),
        flag("TX_EMPTY_CTRL", 8, "Condition étendue de TX_EMPTY"), flag("RX_FIFO_FULL_HLD_CTRL", 9, "Retenir le bus si FIFO RX pleine")});
    describeRegister(d, 4, "IC_TAR", "RW", "Adresse cible du maître", {{"IC_TAR", 0, 10, "Adresse I2C cible"}, flag("GC_OR_START", 10, "General call ou START byte", "General call", "START byte"), flag("SPECIAL", 11, "Activer GC_OR_START")});
    describeRegister(d, 8, "IC_SAR", "RW", "Adresse en mode esclave", {{"IC_SAR", 0, 10, "Adresse propre du contrôleur"}});
    describeRegister(d, 0x10, "IC_DATA_CMD", "Mixte", "RX à la lecture ; données/commande TX à l'écriture", {
        {"DAT", 0, 8, "Octet transmis ou reçu"}, flag("CMD", 8, "Commande maître", "Écrire", "Lire"),
        flag("STOP", 9, "Terminer le transfert par STOP"), flag("RESTART", 10, "Précéder ce transfert par RESTART"),
        flag("FIRST_DATA_BYTE", 11, "Premier octet reçu après l'adresse ; lecture seule")});
    const char* counts[] = {"IC_SS_SCL_HCNT", "IC_SS_SCL_LCNT", "IC_FS_SCL_HCNT", "IC_FS_SCL_LCNT"};
    for (unsigned i = 0; i < 4; ++i) describeRegister(d, 0x14 + i * 4, counts[i], "RW", "Durée SCL en cycles de l'horloge I2C", {{"COUNT", 0, 16, "Compte de cycles"}});
    const std::vector<RegisterField> irq = {
        flag("RX_UNDER", 0, "Lecture RX vide"), flag("RX_OVER", 1, "Débordement RX"), flag("RX_FULL", 2, "Seuil RX atteint"),
        flag("TX_OVER", 3, "Débordement TX"), flag("TX_EMPTY", 4, "Seuil TX atteint"), flag("RD_REQ", 5, "Lecture demandée à l'esclave"),
        flag("TX_ABRT", 6, "Transfert abandonné"), flag("RX_DONE", 7, "Fin de réception"), flag("ACTIVITY", 8, "Activité bus"),
        flag("STOP_DET", 9, "STOP détecté"), flag("START_DET", 10, "START détecté"), flag("GEN_CALL", 11, "General call reçu"), flag("RESTART_DET", 12, "RESTART détecté")};
    describeRegister(d, 0x2c, "IC_INTR_STAT", "RO", "Interruptions masquées", irq);
    describeRegister(d, 0x30, "IC_INTR_MASK", "RW", "Autorisations des interruptions", irq);
    describeRegister(d, 0x34, "IC_RAW_INTR_STAT", "RO", "Interruptions brutes", irq);
    describeRegister(d, 0x38, "IC_RX_TL", "RW", "Seuil FIFO RX", {{"RX_TL", 0, 8, "IRQ quand LEVEL >= RX_TL + 1"}});
    describeRegister(d, 0x3c, "IC_TX_TL", "RW", "Seuil FIFO TX", {{"TX_TL", 0, 8, "IRQ quand LEVEL <= TX_TL"}});
    const char* clear[] = {"INTR", "RX_UNDER", "RX_OVER", "TX_OVER", "RD_REQ", "TX_ABRT", "RX_DONE", "ACTIVITY", "STOP_DET", "START_DET", "GEN_CALL"};
    for (unsigned i = 0; i < 11; ++i) describeRegister(d, 0x40 + i * 4, std::string("IC_CLR_") + clear[i], "RC", "Lecture matérielle acquitte l'interruption ; inspection non destructive uniquement");
    describeRegister(d, 0x6c, "IC_ENABLE", "RW", "Activation du contrôleur", {flag("ENABLE", 0, "Contrôleur actif"), flag("ABORT", 1, "Abandonner un transfert"), flag("TX_CMD_BLOCK", 2, "Bloquer les commandes TX")});
    describeRegister(d, 0x70, "IC_STATUS", "RO", "Activité et FIFO", {
        flag("ACTIVITY", 0, "Activité en cours"), flag("TFNF", 1, "FIFO TX non pleine"), flag("TFE", 2, "FIFO TX vide"),
        flag("RFNE", 3, "FIFO RX non vide"), flag("RFF", 4, "FIFO RX pleine"), flag("MST_ACTIVITY", 5, "Maître actif"), flag("SLV_ACTIVITY", 6, "Esclave actif")});
    describeRegister(d, 0x74, "IC_TXFLR", "RO", "Nombre d'entrées TX", {{"TXFLR", 0, 5, "Niveau FIFO TX"}});
    describeRegister(d, 0x78, "IC_RXFLR", "RO", "Nombre d'entrées RX", {{"RXFLR", 0, 5, "Niveau FIFO RX"}});
    describeRegister(d, 0x80, "IC_TX_ABRT_SOURCE", "RO", "Cause de l'abandon ; lecture IC_CLR_TX_ABRT acquitte les causes");
    describeRegister(d, 0x88, "IC_DMA_CR", "RW", "DMA du contrôleur", {flag("RDMAE", 0, "DMA RX"), flag("TDMAE", 1, "DMA TX")});
    describeRegister(d, 0x9c, "IC_ENABLE_STATUS", "RO", "État réel de l'activation", {flag("IC_EN", 0, "Contrôleur activé")});
}
}
