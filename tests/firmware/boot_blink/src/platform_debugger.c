#include "platform.h"

/* SDK 2.2.0 register offsets; see the reference links in README.md.
 * This backend observes SIO latches only. IO_BANK0/PADS_BANK0, clock setup
 * and electrical LED behaviour are NOT implemented by PicoDbg v0.3.0. */
#define LED_MASK (UINT32_C(1) << 25)
#if BOOT_BLINK_RP2350
#define GPIO_OUT_SET UINT32_C(0xd0000018)
#define GPIO_OUT_CLR UINT32_C(0xd0000020)
#define GPIO_OE_SET  UINT32_C(0xd0000038)
#define TIMER_RAWL   UINT32_C(0x400b0028)
#else
#define GPIO_OUT_SET UINT32_C(0xd0000014)
#define GPIO_OUT_CLR UINT32_C(0xd0000018)
#define GPIO_OE_SET  UINT32_C(0xd0000024)
#define TIMER_RAWL   UINT32_C(0x40054028)
#endif

static volatile uint32_t *reg32(uint32_t address)
{
    return (volatile uint32_t *)(uintptr_t)address;
}

void platform_led_init(void)
{
    *reg32(GPIO_OUT_CLR) = LED_MASK;
    *reg32(GPIO_OE_SET) = LED_MASK;
}

void platform_led_write(bool on)
{
    *reg32(on ? GPIO_OUT_SET : GPIO_OUT_CLR) = LED_MASK;
}

void platform_delay_ms(uint32_t milliseconds)
{
    const uint32_t duration = milliseconds * UINT32_C(1000);
    const uint32_t start = *reg32(TIMER_RAWL);
    /* Unsigned subtraction tolerates the low counter wrapping. The configured
     * delay is <= 60 s, well below the 32-bit timer period. Time is virtual:
     * it advances when the debugger executes, not while its UI is paused. */
    while ((uint32_t)(*reg32(TIMER_RAWL) - start) < duration) {
    }
}
