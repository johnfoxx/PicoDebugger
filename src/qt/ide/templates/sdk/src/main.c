#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

int main(void)
{
#ifdef CYW43_WL_GPIO_LED_PIN
    if (cyw43_arch_init() != 0) {
        return 1;
    }
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif

    bool on = false;
    for (;;) {
        on = !on;
#ifdef CYW43_WL_GPIO_LED_PIN
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
        gpio_put(PICO_DEFAULT_LED_PIN, on);
#endif
        sleep_ms(500);
    }
}
