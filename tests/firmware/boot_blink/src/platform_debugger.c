#include "platform.h"

/* Registres SIO et timer du modèle. Les adresses dépendent de la puce.
 * L'exemple observe les verrous GPIO. Les pads et les horloges relèvent du SDK sur carte. */
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
    /* Accès MMIO en 32 bits. volatile impose chaque lecture ou écriture. */
    return (volatile uint32_t *)(uintptr_t)address;
}

void platform_led_init(void)
{
    /* Placer le verrou à zéro avant d'activer la sortie. */
    *reg32(GPIO_OUT_CLR) = LED_MASK;
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
