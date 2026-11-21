#ifndef BOOT_BLINK_STATE_H
#define BOOT_BLINK_STATE_H

#include <stdint.h>

/* Valeur reconnaissable à l'écran. Ses quatre groupes hexadécimaux codent PICO. */
#define BOOT_DATA_COOKIE UINT32_C(0x5049434f)

/* Valeurs publiées dans boot_stage pour suivre la progression du firmware. */
enum BootStage {
    BOOT_NOT_STARTED = 0,
    BOOT_C_RUNTIME_READY = 1,
    BOOT_LED_READY = 2,
    BOOT_BLINKING = 3,
    BOOT_FAILED = 255
};

/* volatile rend les mises à jour observables en mémoire.
 * Dans PicoDebugger, read32(symbole) lit la valeur à l'adresse du symbole. */
extern volatile uint32_t boot_stage;
extern volatile uint32_t boot_error;       /* 1 pour .data, 2 pour .bss, 3 pour le handler. */
extern volatile uint32_t data_cookie;      /* Valeur initiale non nulle en .data. */
extern volatile uint32_t bss_cookie;       /* Doit valoir zéro à l'entrée de main. */
extern volatile uint32_t led_state;        /* 1 si la LED est allumée. */
extern volatile uint32_t led_transitions;  /* Nombre total de changements d'état. */
extern volatile uint32_t blink_count;      /* Nombre de cycles allumé/éteint terminés. */
extern volatile uint32_t half_period_ms;   /* Durée d'un état de la LED. */

/* Point d'arrêt possible après la mise à jour de la sortie et des compteurs. */
void blink_checkpoint(void);

#endif
