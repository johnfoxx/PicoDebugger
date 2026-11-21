/* Le débogueur charge l'image dans les premiers 256 Kio de SRAM. */
ENTRY(Reset_Handler)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

PHDRS
{
    code PT_LOAD FLAGS(5); /* Lecture et exécution. */
    data PT_LOAD FLAGS(6); /* Lecture et écriture. */
}

SECTIONS
{
    /* Le chargeur lit MSP et PC dans les deux premiers mots. */
    .vectors ORIGIN(RAM) : { KEEP(*(.vectors)) } > RAM :code
    .text : ALIGN(4) { *(.text .text.* .rodata .rodata.*) } > RAM :code

    /* La source des valeurs initiales se trouve dans le même segment SRAM. */
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM :data
    __data_load = LOADADDR(.data);

    /* Le startup efface cette zone. Elle ne consomme pas d'octets dans l'ELF. */
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM :data

    /* La pile descend depuis le haut de cette région de SRAM. */
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 4096, "Pile de 4 Kio insuffisamment reservee")
    ASSERT(SIZEOF(.vectors) == 68 * 4, "Table des vecteurs inattendue")
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .eh_frame* .note.* .comment) }
}
