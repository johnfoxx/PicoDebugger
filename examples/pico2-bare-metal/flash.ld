/* La ROM cherche les métadonnées au début de l'image Flash XIP. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 4M
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

PHDRS
{
    code PT_LOAD FLAGS(5); /* Flash en lecture/exécution. */
    data PT_LOAD FLAGS(6); /* Valeurs initiales en Flash, destination en RAM. */
    bss PT_LOAD FLAGS(6);  /* Réservation en RAM, sans octets dans l'image. */
}

SECTIONS
{
    .vectors ORIGIN(FLASH) : { KEEP(*(.vectors)) } > FLASH :code
    .image_def : ALIGN(4) { KEEP(*(.image_def)) } > FLASH :code
    .text : ALIGN(4) { *(.text .text.* .rodata .rodata.*) } > FLASH :code

    /* Le code utilise l'adresse RAM (VMA). Les octets viennent de la Flash (LMA). */
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT> FLASH :data
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : AT(ADDR(.bss)) ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM :bss

    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 4096, "Pile de 4 Kio insuffisamment reservee")
    ASSERT(SIZEOF(.vectors) == 68 * 4, "Table des vecteurs inattendue")
    ASSERT(SIZEOF(.image_def) == 20, "Bloc PICOBIN inattendu")
    ASSERT(ADDR(.image_def) + SIZEOF(.image_def) <= ORIGIN(FLASH) + 4096,
           "Bloc PICOBIN trop loin du debut de la Flash")
    /DISCARD/ : { *(.ARM.exidx* .ARM.extab* .eh_frame* .note.* .comment) }
}
