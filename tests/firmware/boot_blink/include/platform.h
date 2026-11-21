#ifndef BOOT_BLINK_PLATFORM_H
#define BOOT_BLINK_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

/* Configurer la sortie. Chaque backend fournit ses propres accès matériels. */
void platform_led_init(void);
/* Appliquer le nouvel état de la LED. */
void platform_led_write(bool on);
/* Attendre en temps réel sur la carte, en temps virtuel dans l'émulateur. */
void platform_delay_ms(uint32_t milliseconds);

#endif
