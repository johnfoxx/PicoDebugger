#include "blink_state.h"
#include "platform.h"

#ifndef BOOT_BLINK_HALF_PERIOD_MS
#define BOOT_BLINK_HALF_PERIOD_MS 500
#endif
#if BOOT_BLINK_HALF_PERIOD_MS < 1 || BOOT_BLINK_HALF_PERIOD_MS > 60000
#error "BOOT_BLINK_HALF_PERIOD_MS must be between 1 and 60000"
#endif

/* Témoins de .data. Le démarrage doit restaurer leur valeur initiale. */
volatile uint32_t data_cookie = BOOT_DATA_COOKIE;
volatile uint32_t half_period_ms = BOOT_BLINK_HALF_PERIOD_MS;
/* Témoins de .bss et état du clignotement. Le démarrage met la plage à zéro. */
volatile uint32_t bss_cookie;
volatile uint32_t boot_stage;
volatile uint32_t boot_error;
volatile uint32_t led_state;
volatile uint32_t led_transitions;
volatile uint32_t blink_count;

static void boot_fail(uint32_t reason)
{
    boot_error = reason;
    boot_stage = BOOT_FAILED;
    for (;;) {
        /* Rester ici. Inspecter boot_error et les sections en SRAM. */
    }
}

/* Ce symbole fournit un point d'arrêt après chaque transition.
 * noinline, le build sans LTO et la lecture volatile gardent l'appel visible.
 * À cet endroit, la sortie et les compteurs ont déjà été mis à jour. */
__attribute__((noinline)) void blink_checkpoint(void)
{
    (void)led_state;
}

int main(void)
{
    /* Vérifier la copie de .data et l'effacement de .bss avant toute mise à jour.
     * La carte utilise le startup SDK. L'émulateur utilise le startup de ce projet. */
    if (data_cookie != BOOT_DATA_COOKIE ||
        half_period_ms != BOOT_BLINK_HALF_PERIOD_MS) {
        boot_fail(1);
    }
    if (bss_cookie != 0 || boot_stage != 0 || boot_error != 0 ||
        led_state != 0 || led_transitions != 0 || blink_count != 0) {
        boot_fail(2);
    }
    boot_stage = BOOT_C_RUNTIME_READY;

    platform_led_init();
    boot_stage = BOOT_LED_READY;
    boot_stage = BOOT_BLINKING;

    for (;;) {
        /* Allumer la LED et publier l'état avant l'attente. */
        platform_led_write(true);
        led_state = 1;
        ++led_transitions;
        blink_checkpoint();
        platform_delay_ms(half_period_ms);

        /* Éteindre la LED. Une paire allumé/éteint compte pour un cycle. */
        platform_led_write(false);
        led_state = 0;
        ++led_transitions;
        ++blink_count;
        blink_checkpoint();
        platform_delay_ms(half_period_ms);
    }
}
