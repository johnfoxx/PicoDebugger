#include "memory_init.h"

void boot_init_memory(const uint32_t *data_source,
                      uint32_t *data_begin, uint32_t *data_end,
                      uint32_t *bss_begin, uint32_t *bss_end)
{
    /* No libc, heap or initialized globals are available at this point.
     * Volatile stores prevent replacement of these loops by memcpy/memset. */
    volatile uint32_t *destination = data_begin;
    while (destination != data_end) {
        *destination++ = *data_source++;
    }
    destination = bss_begin;
    while (destination != bss_end) {
        *destination++ = 0;
    }
}
