#ifndef BOOT_BLINK_MEMORY_INIT_H
#define BOOT_BLINK_MEMORY_INIT_H

#include <stdint.h>

/* Word-aligned ranges, with exclusive ends, supplied by the linker script. */
void boot_init_memory(const uint32_t *data_source,
                      uint32_t *data_begin, uint32_t *data_end,
                      uint32_t *bss_begin, uint32_t *bss_end);

#endif
