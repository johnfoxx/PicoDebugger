#include <stdint.h>

/* Le démarrage met ce compteur global à zéro en effaçant .bss.
 * volatile conserve les lectures et les écritures visibles dans le débogueur. */
volatile uint32_t counter;

int main(void)
{
    /* Boucle infinie. Avancer au pas pour observer la valeur changer en SRAM.
     * Après 0xffffffff, l'incrément suivant ramène le compteur à zéro. */
    for (;;) {
        ++counter;
    }
}
