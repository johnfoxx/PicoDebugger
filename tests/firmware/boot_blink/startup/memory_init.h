#ifndef BOOT_BLINK_MEMORY_INIT_H
#define BOOT_BLINK_MEMORY_INIT_H

#include <stdint.h>

/* Le script de lien fournit des adresses alignées sur des mots de 32 bits.
 * data_source pointe sur les valeurs initiales de .data dans l'image.
 * Les deux plages de destination s'arrêtent juste avant leur adresse de fin. */
void boot_init_memory(const uint32_t *data_source,
                      uint32_t *data_begin, uint32_t *data_end,
                      uint32_t *bss_begin, uint32_t *bss_end);

#endif
