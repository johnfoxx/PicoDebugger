#include "memory_init.h"

/* ram.ld fournit ces adresses. Prendre leur adresse avec & dans le startup.
 * Le chargeur a déjà initialisé MSP et PC à partir de la table des vecteurs.
 * Reset_Handler peut donc être une fonction C ordinaire. */
extern uint32_t __stack_top;
extern uint32_t __data_load_start;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern int main(void);

/* Le compilateur sait que ces fonctions gardent le contrôle. */
void Reset_Handler(void) __attribute__((noreturn));
static void Default_Handler(void) __attribute__((noreturn));

typedef void (*Handler)(void);
struct VectorTable {
    const uint32_t *initial_sp;  /* Valeur de MSP avant la première instruction. */
    Handler reset;               /* Première fonction appelée. */
    Handler exceptions[14];       /* Emplacements des exceptions internes restantes. */
    Handler interrupts[52];       /* Emplacements des interruptions externes. */
};

/* Répéter le handler par défaut pour remplir toutes les entrées. */
#define HANDLERS_2 Default_Handler, Default_Handler
#define HANDLERS_4 HANDLERS_2, HANDLERS_2
#define HANDLERS_8 HANDLERS_4, HANDLERS_4
#define HANDLERS_16 HANDLERS_8, HANDLERS_8
#define HANDLERS_32 HANDLERS_16, HANDLERS_16

/* Les 68 entrées couvrent le profil le plus large. Le RP2040 ignore le surplus.
 * L'attribut place la table dans .vectors et demande un alignement de 512 octets.
 * Les pointeurs de fonction portent déjà le bit Thumb dans l'ELF. */
__attribute__((section(".vectors"), used, aligned(512)))
const struct VectorTable __vectors = {
    &__stack_top,
    Reset_Handler,
    {HANDLERS_8, HANDLERS_4, HANDLERS_2},
    {HANDLERS_32, HANDLERS_16, HANDLERS_4}
};

void Reset_Handler(void)
{
    /* Mettre en place les variables globales avant d'exécuter l'application. */
    boot_init_memory(&__data_load_start, &__data_start, &__data_end,
                     &__bss_start, &__bss_end);

    /* Écrire l'adresse des vecteurs dans VTOR, le registre de la table d'exceptions.
     * Ce démarrage minimal laisse les IRQ désactivées dans le contrôleur.
     * La configuration de sécurité du RP2350 relève du démarrage complet de la carte. */
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xe000ed08) = (uint32_t)(uintptr_t)&__vectors;
    /* Lancer le C applicatif. Si main retourne, rester dans le handler par défaut. */
    (void)main();
    Default_Handler();
}

static void Default_Handler(void)
{
    /* Boucler ici pour laisser le contexte de l'exception observable au débogueur. */
    for (;;) {
    }
}
