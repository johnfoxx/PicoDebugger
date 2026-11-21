#include <stdint.h>

#define BIT(n)  (UINT32_C(1) << (n))
#define MMIO(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define RESETS     UINT32_C(0x40020000)
#define IO_BANK0   UINT32_C(0x40028000)
#define PADS_BANK0 UINT32_C(0x40038000)
#define SIO        UINT32_C(0xd0000000)
#define VTOR       MMIO(0xe000ed08u)

enum { LED_GP = 15 };
#define LED_MASK BIT(LED_GP)

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
    handler_t irq[52];
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
    /* La sonde a placé MSP et PC. Ce premier exercice n'utilise aucune IRQ. */
    __asm__ volatile ("cpsid i" ::: "memory");
    VTOR = (uint32_t)(uintptr_t)&vectors;
    __asm__ volatile ("dsb sy\n\tisb sy" ::: "memory");

    /* En SRAM, la source de .data peut être la destination elle-même. */
    uintptr_t src = (uintptr_t)&__data_load;
    for (uintptr_t dst = (uintptr_t)&__data_start; dst < (uintptr_t)&__data_end; dst += 4u) {
        MMIO(dst) = *(const uint32_t *)src;
        src += 4u;
    }
    for (uintptr_t dst = (uintptr_t)&__bss_start; dst < (uintptr_t)&__bss_end; dst += 4u) {
        MMIO(dst) = 0;
    }

    (void)main();
    Default_Handler();
}

static int configurer_gp15(void)
{
    const uint32_t blocs = BIT(6) | BIT(9); /* IO_BANK0 et PADS_BANK0. */
    MMIO(RESETS + 0x3000u) = blocs;         /* Alias CLR de RESET. */
    for (uint32_t essais = 1000000; essais != 0; --essais) {
        if ((MMIO(RESETS + 0x08u) & blocs) == blocs) {
            /* Préparer un niveau bas et une sortie désactivée avant le mux. */
            MMIO(SIO + 0x040u) = LED_MASK; /* GPIO_OE_CLR. */
            MMIO(SIO + 0x020u) = LED_MASK; /* GPIO_OUT_CLR. */
            MMIO(IO_BANK0 + 4u + 8u * LED_GP) = 5u; /* FUNCSEL SIO. */

            /* Retirer isolation, désactivation de sortie et résistances internes. */
            MMIO(PADS_BANK0 + 0x3000u + 4u + 4u * LED_GP) =
                BIT(8) | BIT(7) | BIT(3) | BIT(2);
            MMIO(SIO + 0x038u) = LED_MASK; /* GPIO_OE_SET. */
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
        MMIO(SIO + 0x018u) = LED_MASK; /* GPIO_OUT_SET. */
        etape = 1;
        attendre_visuellement();

        MMIO(SIO + 0x020u) = LED_MASK; /* GPIO_OUT_CLR. */
        etape = 2;
        attendre_visuellement();
    }
}
