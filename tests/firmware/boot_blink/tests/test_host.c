#include "blink_state.h"
#include "memory_init.h"
#include "platform.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static jmp_buf finished;
static unsigned transitions;
static unsigned delays;
static bool initialized;
static bool output;
extern int firmware_main(void);

void platform_led_init(void)
{
    assert(boot_stage == BOOT_C_RUNTIME_READY);
    assert(!initialized);
    initialized = true;
    output = false;
}

void platform_led_write(bool on)
{
    assert(initialized && boot_stage == BOOT_BLINKING);
    assert(on != output);
    output = on;
    ++transitions;
}

void platform_delay_ms(uint32_t milliseconds)
{
    assert(milliseconds == 1);
    assert(led_state == (uint32_t)output);
    assert(led_transitions == transitions);
    assert(blink_count == transitions / 2);
    ++delays;
    assert(delays == transitions);
    if (delays == 6) {
        longjmp(finished, 1);
    }
}

static void test_memory(void)
{
    const uint32_t flash[] = {BOOT_DATA_COOKIE, 1, 0xffffffff};
    uint32_t ram_data[] = {0x1234, 0xdead, 0xbeef, 0xabcd, 0x5678};
    uint32_t ram_bss[] = {0x1234, 0xdead, 0xbeef, 0xabcd, 0x5678};
    boot_init_memory(flash, ram_data + 1, ram_data + 4, ram_bss + 1, ram_bss + 4);
    assert(memcmp(ram_data + 1, flash, sizeof flash) == 0);
    assert(ram_bss[1] == 0 && ram_bss[2] == 0 && ram_bss[3] == 0);
    assert(ram_data[0] == 0x1234 && ram_data[4] == 0x5678);
    assert(ram_bss[0] == 0x1234 && ram_bss[4] == 0x5678);
    /* Empty ranges must not touch their sentinels or dereference the source. */
    boot_init_memory(NULL, ram_data, ram_data, ram_bss, ram_bss);
    assert(ram_data[0] == 0x1234 && ram_bss[0] == 0x1234);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "memory") == 0) {
        test_memory();
    } else {
        assert(strcmp(argv[1], "blink") == 0);
        if (setjmp(finished) == 0) {
            (void)firmware_main();
            assert(!"The firmware must keep blinking");
        }
        assert(blink_count == 3 && led_state == 0 && boot_error == 0);
    }
    puts("OK");
    return 0;
}
