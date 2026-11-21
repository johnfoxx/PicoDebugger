#include "memory_init.h"
#include "blink_state.h"

/* debugger.ld fournit les adresses des plages Flash et SRAM.
 * Prendre leur adresse avec & pour les transmettre au startup.
 * Le chargeur a déjà réglé MSP et PC à partir des vecteurs.
 * Reset_Handler peut s'exécuter comme une fonction C ordinaire. */
extern uint32_t __stack_top;
extern uint32_t __data_load_start;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern int main(void);

/* Les deux handlers gardent le contrôle une fois appelés. */
void Reset_Handler(void) __attribute__((noreturn));
static void Default_Handler(void) __attribute__((noreturn));

typedef void (*Handler)(void);
struct VectorTable {
    const uint32_t *initial_sp;  /* Première entrée : valeur de MSP. */
    Handler reset;               /* Deuxième entrée : point de départ. */
    Handler exceptions[14];       /* Exceptions du cœur après Reset. */
    Handler interrupts[52];       /* Interruptions de la puce. */
};

/* Répéter le même handler pour tous les emplacements réservés. */
#define HANDLERS_2 Default_Handler, Default_Handler
#define HANDLERS_4 HANDLERS_2, HANDLERS_2
#define HANDLERS_8 HANDLERS_4, HANDLERS_4
#define HANDLERS_16 HANDLERS_8, HANDLERS_8
#define HANDLERS_32 HANDLERS_16, HANDLERS_16

/* Les 68 entrées couvrent le RP2350. Le RP2040 ignore le surplus.
 * L'attribut demande un alignement de 512 octets. Le script place la table.
 * Les pointeurs de fonction portent le bit Thumb après le lien. */
__attribute__((section(".vectors"), used, aligned(512)))
const struct VectorTable __vectors = {
    &__stack_top,
    Reset_Handler,
    {HANDLERS_8, HANDLERS_4, HANDLERS_2},
    {HANDLERS_32, HANDLERS_16, HANDLERS_4}
};

void Reset_Handler(void)
{
    /* Copier .data de Flash en SRAM, puis effacer .bss avant main. */
    boot_init_memory(&__data_load_start, &__data_start, &__data_end,
                     &__bss_start, &__bss_end);

    /* VTOR indique au cœur où lire les vecteurs d'exception.
     * Le contrôleur IRQ reste dans son état initial. */
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xe000ed08) = (uint32_t)(uintptr_t)&__vectors;
    (void)main();
    /* Signaler aussi une sortie inattendue de main. */
    Default_Handler();
}

static void Default_Handler(void)
{
    /* Publier la cause. Boucler pour permettre l'inspection du contexte. */
    boot_error = 3;
    boot_stage = BOOT_FAILED;
    for (;;) {
    }
}
