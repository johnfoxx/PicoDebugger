#include <stdint.h>

/* Bloc PICOBIN minimal, pour une carte sans politique de signature imposée.
 * Les vecteurs à la base de la Flash donnent le MSP et le PC d'entrée. */
__attribute__((used, aligned(4), section(".image_def")))
const uint32_t image_def[] = {
    UINT32_C(0xffffded3), /* Marqueur de début. */
    UINT32_C(0x10210142), /* IMAGE_TYPE, 1 mot, exécutable Arm Secure RP2350. */
    UINT32_C(0x000001ff), /* LAST, 1 mot d'éléments dans le bloc. */
    UINT32_C(0),          /* Déplacement nul vers ce bloc, boucle d'un seul bloc. */
    UINT32_C(0xab123579), /* Marqueur de fin. */
};
