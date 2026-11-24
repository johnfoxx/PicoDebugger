#include "platform.h"

/* La LED est sur GP25. Les adresses des registres dépendent de la puce. */
#define LED_MASK (UINT32_C(1) << 25)
#if BOOT_BLINK_RP2350
#define RESETS_CLR   UINT32_C(0x40023000)
#define RESETS_DONE  UINT32_C(0x40020008)
#define RESET_BLOCKS ((UINT32_C(1) << 6) | (UINT32_C(1) << 9) | (UINT32_C(1) << 23))
#define GPIO25_CTRL  UINT32_C(0x400280cc)
#define GPIO25_PAD_CLR UINT32_C(0x4003b068)
#define GPIO_OUT_SET UINT32_C(0xd0000018)
#define GPIO_OUT_CLR UINT32_C(0xd0000020)
#define GPIO_OE_SET  UINT32_C(0xd0000038)
#define TIMER_RAWL   UINT32_C(0x400b0028)
#else
#define RESETS_CLR   UINT32_C(0x4000f000)
#define RESETS_DONE  UINT32_C(0x4000c008)
#define RESET_BLOCKS ((UINT32_C(1) << 5) | (UINT32_C(1) << 8) | (UINT32_C(1) << 21))
#define GPIO25_CTRL  UINT32_C(0x400140cc)
#define GPIO25_PAD_CLR UINT32_C(0x4001f068)
#define GPIO_OUT_SET UINT32_C(0xd0000014)
#define GPIO_OUT_CLR UINT32_C(0xd0000018)
#define GPIO_OE_SET  UINT32_C(0xd0000024)
#define TIMER_RAWL   UINT32_C(0x40054028)
#endif

static volatile uint32_t *reg32(uint32_t address)
{
    /* Accès MMIO en 32 bits. volatile impose chaque lecture ou écriture. */
    return (volatile uint32_t *)(uintptr_t)address;
}

void platform_led_init(void)
{
    /* Libérer IO_BANK0, PADS_BANK0 et le timer utilisé pour les délais. */
    *reg32(RESETS_CLR) = RESET_BLOCKS;
    while ((*reg32(RESETS_DONE) & RESET_BLOCKS) != RESET_BLOCKS) {
    }
    /* Préparer un niveau bas puis raccorder la sortie SIO au pad. */
    *reg32(GPIO_OUT_CLR) = LED_MASK;
    *reg32(GPIO25_CTRL) = 5; /* FUNCSEL = SIO */
#if BOOT_BLINK_RP2350
    *reg32(GPIO25_PAD_CLR) = UINT32_C(0x180); /* ISO et OD */
#else
    *reg32(GPIO25_PAD_CLR) = UINT32_C(0x80); /* OD */
#endif
    *reg32(GPIO_OE_SET) = LED_MASK;
}

void platform_led_write(bool on)
{
    /* Les alias SET et CLR modifient GPIO25 en laissant les autres bits intacts. */
    *reg32(on ? GPIO_OUT_SET : GPIO_OUT_CLR) = LED_MASK;
}

void platform_delay_ms(uint32_t milliseconds)
{
    const uint32_t duration = milliseconds * UINT32_C(1000);
    const uint32_t start = *reg32(TIMER_RAWL);
    /* Le registre bas compte des microsecondes. La soustraction non signée
     * reste correcte si ce compteur déborde pendant les 60 secondes au plus.
     * Le temps virtuel ne progresse pas pendant la pause. */
    while ((uint32_t)(*reg32(TIMER_RAWL) - start) < duration) {
    }
}
