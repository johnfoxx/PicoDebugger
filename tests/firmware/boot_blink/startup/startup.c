#include "memory_init.h"
#include "blink_state.h"

/* Linker symbols denote addresses, not C variables to read. On entry the
 * loader has already established MSP and Thumb PC from the vector table.
 * Consequently Reset_Handler can be an ordinary C function: no naked function,
 * manual stack manipulation or assembly trampoline is needed here. */
extern uint32_t __stack_top;
extern uint32_t __data_load_start;
extern uint32_t __data_start;
extern uint32_t __data_end;
extern uint32_t __bss_start;
extern uint32_t __bss_end;
extern int main(void);

void Reset_Handler(void) __attribute__((noreturn));
static void Default_Handler(void) __attribute__((noreturn));

typedef void (*Handler)(void);
struct VectorTable {
    const uint32_t *initial_sp;
    Handler reset;
    Handler exceptions[14];
    Handler interrupts[52];
};

#define HANDLERS_2 Default_Handler, Default_Handler
#define HANDLERS_4 HANDLERS_2, HANDLERS_2
#define HANDLERS_8 HANDLERS_4, HANDLERS_4
#define HANDLERS_16 HANDLERS_8, HANDLERS_8
#define HANDLERS_32 HANDLERS_16, HANDLERS_16

/* 68 vectors cover the larger profile; the unused RP2040 IRQ entries are
 * harmless. The linker aligns the table to 512 bytes. Function pointers carry
 * the Thumb bit through ARM ELF relocations; never add it manually here. */
__attribute__((section(".vectors"), used, aligned(512)))
const struct VectorTable __vectors = {
    &__stack_top,
    Reset_Handler,
    {HANDLERS_8, HANDLERS_4, HANDLERS_2},
    {HANDLERS_32, HANDLERS_16, HANDLERS_4}
};

void Reset_Handler(void)
{
    boot_init_memory(&__data_load_start, &__data_start, &__data_end,
                     &__bss_start, &__bss_end);

    /* Keep VTOR consistent with the image even when inspecting startup steps.
     * No IRQ is enabled. This does not implement the RP2350 security startup. */
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xe000ed08) = (uint32_t)(uintptr_t)&__vectors;
    (void)main();
    Default_Handler();
}

static void Default_Handler(void)
{
    boot_error = 3;
    boot_stage = BOOT_FAILED;
    for (;;) {
    }
}
