#include "platform.h"
#include "hardware/gpio.h"
#include "pico/time.h"

#if !defined(PICO_DEFAULT_LED_PIN) || PICO_DEFAULT_LED_PIN != 25
#error "This example targets Pico / Pico 2 without Wi-Fi (LED on GPIO25)."
#endif

void platform_led_init(void)
{
    /* gpio_init also configures the pad and routes the pin to SIO. Set the
     * output latch low before enabling the driver to avoid a startup pulse. */
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_put(PICO_DEFAULT_LED_PIN, false);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
}

void platform_led_write(bool on)
{
    gpio_put(PICO_DEFAULT_LED_PIN, on);
}

void platform_delay_ms(uint32_t milliseconds)
{
    sleep_ms(milliseconds);
}
