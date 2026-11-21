#include "platform.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#if !defined(PICO_DEFAULT_LED_PIN) || PICO_DEFAULT_LED_PIN != 25
#error "This example targets Pico / Pico 2 without Wi-Fi (LED on GPIO25)."
#endif

void platform_led_init(void)
{
    /* gpio_init configure le pad et route la broche vers SIO.
     * Placer la sortie à zéro avant de l'activer évite une impulsion au démarrage. */
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

void platform_led_write(bool on)
{
    /* La LED de GPIO25 s'allume au niveau haut. */
    gpio_put(PICO_DEFAULT_LED_PIN, on);
}

void platform_delay_ms(uint32_t milliseconds)
{
    /* Attendre avec la source de temps du SDK, en millisecondes. */
    sleep_ms(milliseconds);
}
