#include "rp2350_regs.h"

enum { LED_GP = 15 };
#define LED_MASK RP2350_BIT(LED_GP)

/* Ces symboles représentent les adresses choisies par sram.ld. */
extern uint32_t __stack_top, __data_start, __data_end, __data_load;
extern uint32_t __bss_start, __bss_end;

void Reset_Handler(void);
void Default_Handler(void);
int main(void);

typedef void (*handler_t)(void);

#define D2  Default_Handler, Default_Handler
#define D4  D2, D2
#define D8  D4, D4
#define D16 D8, D8
#define D32 D16, D16

/* MSP puis l'entrée Thumb, les 14 autres exceptions et les 52 vecteurs IRQ. */
__attribute__((used, section(".vectors"), aligned(512)))
const struct {
    uint32_t *msp;
    handler_t reset;
    handler_t exceptions[14];
    handler_t irq[RP2350_IRQ_SLOTS];
} vectors = {&__stack_top, Reset_Handler, {D8, D4, D2}, {D32, D16, D4}};

/* Deux témoins en mémoire. Poser un breakpoint dans main puis les inspecter. */
volatile uint32_t temoin_data = UINT32_C(0x2350);
volatile uint32_t etape;

void Default_Handler(void)
{
    /* Garder le contexte stable pour un arrêt manuel dans le débogueur. */
    for (;;) {}
}

void Reset_Handler(void)
{
    /* La sonde ou la Boot ROM a préparé MSP et PC. Masquer les IRQ du test. */
    __asm__ volatile ("cpsid i" ::: "memory");
    RP2350_REG32(RP2350_SCB_VTOR) = (uint32_t)(uintptr_t)&vectors;
    __asm__ volatile ("dsb sy\n\tisb sy" ::: "memory");

    /* En SRAM, la source de .data peut être la destination elle-même. */
    uintptr_t src = (uintptr_t)&__data_load;
    for (uintptr_t dst = (uintptr_t)&__data_start; dst < (uintptr_t)&__data_end; dst += 4u) {
        RP2350_REG32(dst) = *(const uint32_t *)src;
        src += 4u;
    }
    for (uintptr_t dst = (uintptr_t)&__bss_start; dst < (uintptr_t)&__bss_end; dst += 4u) {
        RP2350_REG32(dst) = 0;
    }

    (void)main();
    Default_Handler();
}

static int configurer_gp15(void)
{
    const uint32_t blocs = RP2350_RESET_IO_BANK0 | RP2350_RESET_PADS_BANK0;
    RP2350_REG32(RP2350_ALIAS_CLR(RP2350_RESETS_RESET)) = blocs;
    for (uint32_t essais = 1000000; essais != 0; --essais) {
        if ((RP2350_RO32(RP2350_RESETS_RESET_DONE) & blocs) == blocs) {
            /* Préparer un niveau bas et une sortie désactivée avant le mux. */
            RP2350_REG32(RP2350_SIO_GPIO_OE_CLR) = LED_MASK;
            RP2350_REG32(RP2350_SIO_GPIO_OUT_CLR) = LED_MASK;
            RP2350_REG32(RP2350_GPIO_CTRL(LED_GP)) = RP2350_FUNC_SIO;

            /* Retirer isolation, désactivation de sortie et résistances internes. */
            RP2350_REG32(RP2350_ALIAS_CLR(RP2350_PAD_GPIO(LED_GP))) =
                RP2350_PAD_ISO | RP2350_PAD_OD | RP2350_PAD_PUE | RP2350_PAD_PDE;
            RP2350_REG32(RP2350_SIO_GPIO_OE_SET) = LED_MASK;
            return 1;
        }
    }
    return 0;
}

static void attendre_visuellement(void)
{
    /* Pause approximative. Elle ne représente aucune durée calibrée. */
    for (volatile uint32_t i = 0; i < 2000000u; ++i) {}
}

int main(void)
{
    if (!configurer_gp15()) {
        etape = 0xffu; /* Lire RESET_DONE et l'état des horloges. */
        return 1;
    }

    for (;;) {
        RP2350_REG32(RP2350_SIO_GPIO_OUT_SET) = LED_MASK;
        etape = 1;
        attendre_visuellement();

        RP2350_REG32(RP2350_SIO_GPIO_OUT_CLR) = LED_MASK;
        etape = 2;
        attendre_visuellement();
    }
}
