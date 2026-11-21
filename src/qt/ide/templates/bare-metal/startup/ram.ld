/* Adresse de démarrage enregistrée dans l'en-tête ELF. */
ENTRY(Reset_Handler)

/* Utiliser les 256 premiers Kio de SRAM, présents sur RP2040 et RP2350.
 * Le chargeur ELF ou le transfert SWD en RAM place l'image à cette adresse. */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}

/* Deux segments chargeables. 5 signifie lecture/exécution. 6 signifie lecture/écriture. */
PHDRS
{
    code PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* Le chargeur trouve la pile initiale et le PC au début de la SRAM.
     * KEEP conserve cette table lorsque le lien élimine les sections inutilisées. */
    .vectors ORIGIN(RAM) :
    {
        KEEP(*(.vectors))
    } > RAM :code

    /* Instructions et constantes en lecture seule. Aligner la fin sur un mot de 32 bits. */
    .text : ALIGN(4)
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > RAM :code

    /* Variables avec une valeur initiale. Les symboles délimitent leur plage en SRAM. */
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM :data
    /* Ici la source et la destination de .data coïncident. Le startup garde la même
     * interface que pour une image dont les valeurs initiales viennent de la Flash. */
    __data_load_start = LOADADDR(.data);

    /* Réserver la place des variables à zéro. Le fichier ELF ne stocke pas ces octets.
     * Le startup efface cette plage avant d'appeler main. */
    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM :data

    /* La pile descend depuis la fin de la région. Garder au moins 4 Kio libres.
     * Vérifier aussi la taille de la table des vecteurs ARM. */
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    ASSERT(__bss_end <= __stack_top - 4096, "RAM overlaps the stack")
    ASSERT(SIZEOF(.vectors) == 68 * 4, "Expected 32-bit ARM vector entries")
    /* Écarter les notes et les tables de déroulage. Conserver les sections de debug DWARF. */
    /DISCARD/ : { *(.comment .note.* .eh_frame* .ARM.exidx* .ARM.extab*) }
}
