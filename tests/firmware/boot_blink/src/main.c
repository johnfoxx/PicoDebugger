#include "blink_state.h"
#include "platform.h"

#ifndef BOOT_BLINK_HALF_PERIOD_MS
#define BOOT_BLINK_HALF_PERIOD_MS 500
#endif
#if BOOT_BLINK_HALF_PERIOD_MS < 1 || BOOT_BLINK_HALF_PERIOD_MS > 60000
#error "BOOT_BLINK_HALF_PERIOD_MS must be between 1 and 60000"
#endif

volatile uint32_t data_cookie = BOOT_DATA_COOKIE;
volatile uint32_t half_period_ms = BOOT_BLINK_HALF_PERIOD_MS;
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
        /* Deliberately stay here: inspect boot_error and the RAM sections. */
    }
}

/* A stable breakpoint after each transition, without injecting a BKPT into
 * the firmware. Calls are retained (noinline, no LTO, separate observable state).
 * Reaching this symbol means SIO and all the counters already agree. */
__attribute__((noinline)) void blink_checkpoint(void)
{
    (void)led_state;
}

int main(void)
{
    /* These checks run before overwriting any of the .bss probes. The real
     * board uses the SDK startup; the debugger uses our small C startup. */
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
        platform_led_write(true);
        led_state = 1;
        ++led_transitions;
        blink_checkpoint();
        platform_delay_ms(half_period_ms);

        platform_led_write(false);
        led_state = 0;
        ++led_transitions;
        ++blink_count;
        blink_checkpoint();
        platform_delay_ms(half_period_ms);
    }
}
