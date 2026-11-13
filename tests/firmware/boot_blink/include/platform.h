#ifndef BOOT_BLINK_PLATFORM_H
#define BOOT_BLINK_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* The application is shared; only GPIO setup and the time source differ. */
void platform_led_init(void);
void platform_led_write(bool on);
void platform_delay_ms(uint32_t milliseconds);

#endif
