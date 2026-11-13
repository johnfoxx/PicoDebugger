#ifndef BOOT_BLINK_STATE_H
#define BOOT_BLINK_STATE_H

#include <stdint.h>

#define BOOT_DATA_COOKIE UINT32_C(0x5049434f)

enum BootStage {
    BOOT_NOT_STARTED = 0,
    BOOT_C_RUNTIME_READY = 1,
    BOOT_LED_READY = 2,
    BOOT_BLINKING = 3,
    BOOT_FAILED = 255
};

/* Volatile keeps these symbols observable even with compiler optimisation.
 * In PicoDbg, use read32(symbol): a symbol alone denotes its address. */
extern volatile uint32_t boot_stage;
extern volatile uint32_t boot_error;       /* 1: .data, 2: .bss, 3: exception */
extern volatile uint32_t data_cookie;      /* Nonzero initial value in .data. */
extern volatile uint32_t bss_cookie;       /* Must be zero when main starts. */
extern volatile uint32_t led_state;
extern volatile uint32_t led_transitions;
extern volatile uint32_t blink_count;      /* Completed on/off pairs. */
extern volatile uint32_t half_period_ms;

void blink_checkpoint(void);

#endif
