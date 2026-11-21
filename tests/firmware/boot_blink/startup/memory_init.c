#include "memory_init.h"

void boot_init_memory(const uint32_t *data_source,
                      uint32_t *data_begin, uint32_t *data_end,
                      uint32_t *bss_begin, uint32_t *bss_end)
{
    /* La bibliothèque C et les variables globales ne sont pas prêtes.
     * Les écritures volatile empêchent le compilateur d'introduire memcpy ou memset. */
    volatile uint32_t *destination = data_begin;
    /* Copier .data mot par mot. La source peut déjà être en SRAM. */
    while (destination != data_end) {
        *destination++ = *data_source++;
    }
    /* Mettre .bss à zéro. Une plage vide ne provoque aucune écriture. */
    destination = bss_begin;
    while (destination != bss_end) {
        *destination++ = 0;
    }
}
