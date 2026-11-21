#include "pico/stdlib.h"

/* Le profil W fournit une broche de LED sur le composant CYW43. */
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

int main(void)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    /* Initialiser le pilote avant le premier accès au GPIO du CYW43.
     * Quitter avec un code d'erreur si le composant ne démarre pas. */
    if (cyw43_arch_init() != 0) {
        return 1;
    }
#else
    /* Les autres profils utilisent une broche GPIO du microcontrôleur. */
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    /* Inverser l'état à chaque passage. Le premier passage allume la LED. */
    bool on = false;
    for (;;) {
        on = !on;
#ifdef CYW43_WL_GPIO_LED_PIN
        /* Écrire sur la broche du composant Wi-Fi. */
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
        /* Écrire sur la broche GPIO de la carte. */
        gpio_put(PICO_DEFAULT_LED_PIN, on);
#endif
        /* Garder chaque état 500 ms pour obtenir un cycle d'une seconde. */
        sleep_ms(500);
    }
}
