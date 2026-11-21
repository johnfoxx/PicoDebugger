#ifndef PICODEBUGGER_RP2350_REGS_H
#define PICODEBUGGER_RP2350_REGS_H

#include <stdint.h>

/*
 * Carte de registres du RP2350A pour Raspberry Pi Pico 2 et Pico 2 W.
 * Les noms de blocs sont ceux de la fiche technique. Les constantes sont
 * des adresses physiques, utilisables en C et en C++ sans Pico SDK.
 *
 * Les deux cartes partagent les registres du RP2350A. Leur câblage diffère.
 * Ce fichier fournit les bases et les registres utiles pour écrire un pilote.
 * Pour les autres champs, les valeurs après reset et les errata, consulter
 * la fiche RP2350 et les en-têtes 2.2.0 référencés dans le guide PicoDebugger.
 *
 * Les adresses PPB ci-dessous ciblent le Cortex-M33. Les permissions,
 * horloges et resets dépendent de l'état initial de la puce et de ton firmware.
 */

/* MMIO = entrée/sortie mappée en mémoire. Une affectation à REG32 écrit
 * un mot de 32 bits. RO32 exprime une lecture seule côté C. Même une lecture
 * peut déplacer une FIFO ou acquérir un verrou. Utiliser des adresses alignées.
 * volatile matérialise les accès. Il ne synchronise ni deux cœurs ni le DMA. */
#define RP2350_REG32(adresse) (*(volatile uint32_t *)(uintptr_t)(adresse))
#define RP2350_RO32(adresse)  (*(volatile const uint32_t *)(uintptr_t)(adresse))

/* BIT accepte les indices 0 à 31. Le masque de champ inclut déjà son décalage.
 * FIELD_PREP prépare des bits. FIELD_GET les ramène en bas d'un mot lu. */
#define RP2350_BIT(n)                      (UINT32_C(1) << (n))
#define RP2350_FIELD_PREP(masque, bit, v)   (((uint32_t)(v) << (bit)) & (uint32_t)(masque))
#define RP2350_FIELD_GET(masque, bit, mot)  (((uint32_t)(mot) & (uint32_t)(masque)) >> (bit))

/* Alias de registre lorsque le bloc les documente. Écrire le masque seul.
 * SET positionne, CLR efface et XOR inverse les bits sélectionnés.
 * Pour un statut W1C, écrire directement le masque à son adresse normale.
 * SIO a ses propres registres SET/CLR/XOR, indiqués plus bas. */
#define RP2350_ALIAS_XOR(reg) ((uintptr_t)(reg) + 0x1000u)
#define RP2350_ALIAS_SET(reg) ((uintptr_t)(reg) + 0x2000u)
#define RP2350_ALIAS_CLR(reg) ((uintptr_t)(reg) + 0x3000u)

/* Carte mémoire. XIP signifie exécution ou lecture directe dans la Flash. */
#define RP2350_ROM_BASE                    0x00000000u
#define RP2350_XIP_BASE                    0x10000000u
#define RP2350_XIP_SRAM_BASE               0x13ffc000u
#define RP2350_XIP_NOCACHE_BASE            0x14000000u
#define RP2350_XIP_MAINTENANCE_BASE        0x18000000u
#define RP2350_XIP_NOCACHE_RAW_BASE        0x1c000000u
#define RP2350_SRAM_BASE                   0x20000000u
#define RP2350_SRAM8_BASE                  0x20080000u
#define RP2350_SRAM9_BASE                  0x20081000u
#define RP2350_SRAM_END                    0x20082000u /* Fin exclusive. */
#define PICO2_FLASH_SIZE                   (4u * 1024u * 1024u)
#define PICO2_W_FLASH_SIZE                 PICO2_FLASH_SIZE

/* Bases des blocs. Ajouter l'offset d'un registre documenté avant l'accès. */
#define RP2350_SYSINFO_BASE                0x40000000u
#define RP2350_SYSCFG_BASE                 0x40008000u
#define RP2350_CLOCKS_BASE                 0x40010000u
#define RP2350_PSM_BASE                    0x40018000u
#define RP2350_RESETS_BASE                 0x40020000u
#define RP2350_IO_BANK0_BASE               0x40028000u
#define RP2350_IO_QSPI_BASE                0x40030000u
#define RP2350_PADS_BANK0_BASE             0x40038000u
#define RP2350_PADS_QSPI_BASE              0x40040000u
#define RP2350_XOSC_BASE                   0x40048000u
#define RP2350_PLL_SYS_BASE                0x40050000u
#define RP2350_PLL_USB_BASE                0x40058000u
#define RP2350_ACCESSCTRL_BASE             0x40060000u
#define RP2350_BUSCTRL_BASE                0x40068000u
#define RP2350_UART0_BASE                  0x40070000u
#define RP2350_UART1_BASE                  0x40078000u
#define RP2350_SPI0_BASE                   0x40080000u
#define RP2350_SPI1_BASE                   0x40088000u
#define RP2350_I2C0_BASE                   0x40090000u
#define RP2350_I2C1_BASE                   0x40098000u
#define RP2350_ADC_BASE                    0x400a0000u
#define RP2350_PWM_BASE                    0x400a8000u
#define RP2350_TIMER0_BASE                 0x400b0000u
#define RP2350_TIMER1_BASE                 0x400b8000u
#define RP2350_HSTX_CTRL_BASE              0x400c0000u
#define RP2350_XIP_CTRL_BASE               0x400c8000u
#define RP2350_XIP_QMI_BASE                0x400d0000u
#define RP2350_WATCHDOG_BASE               0x400d8000u
#define RP2350_BOOTRAM_BASE                0x400e0000u
#define RP2350_ROSC_BASE                   0x400e8000u
#define RP2350_TRNG_BASE                   0x400f0000u
#define RP2350_SHA256_BASE                 0x400f8000u
#define RP2350_POWMAN_BASE                 0x40100000u
#define RP2350_TICKS_BASE                  0x40108000u
#define RP2350_OTP_BASE                    0x40120000u
#define RP2350_OTP_DATA_BASE               0x40130000u
#define RP2350_OTP_DATA_RAW_BASE           0x40134000u
#define RP2350_OTP_DATA_GUARDED_BASE       0x40138000u
#define RP2350_OTP_DATA_RAW_GUARDED_BASE   0x4013c000u
#define RP2350_CORESIGHT_BASE              0x40140000u
#define RP2350_DFT_BASE                    0x40150000u
#define RP2350_GLITCH_DETECTOR_BASE        0x40158000u
#define RP2350_TBMAN_BASE                  0x40160000u
#define RP2350_DMA_BASE                    0x50000000u
#define RP2350_USB_DPRAM_BASE              0x50100000u
#define RP2350_USB_REGS_BASE               0x50110000u
#define RP2350_PIO0_BASE                   0x50200000u
#define RP2350_PIO1_BASE                   0x50300000u
#define RP2350_PIO2_BASE                   0x50400000u
#define RP2350_XIP_AUX_BASE                0x50500000u
#define RP2350_HSTX_FIFO_BASE              0x50600000u
#define RP2350_CORESIGHT_TRACE_BASE        0x50700000u
#define RP2350_SIO_BASE                    0xd0000000u
#define RP2350_SIO_NONSEC_BASE             0xd0020000u
#define RP2350_PPB_BASE                    0xe0000000u
#define RP2350_PPB_NONSEC_BASE             0xe0020000u
#define RP2350_EPPB_BASE                   0xe0080000u

/* Brochage logique des cartes. Un numéro GP diffère du numéro du connecteur.
 * Les broches internes de la version W restent réservées à la radio.
 * Sur W, la LED intégrée est une sortie du CYW43439, accessible via son pilote.
 * GP29 sert à mesurer VSYS/3. Sur W, cette mesure partage l'horloge radio. */
#define PICO2_XOSC_HZ                      UINT32_C(12000000)
#define PICO2_W_XOSC_HZ                    PICO2_XOSC_HZ
#define PICO2_LED_GPIO                     25u
#define PICO2_W_LED_CYW43_GPIO             0u
#define PICO2_W_WL_ON_GPIO                 23u
#define PICO2_W_WL_DATA_GPIO               24u
#define PICO2_W_WL_CS_GPIO                 25u
#define PICO2_W_WL_CLK_GPIO                29u
#define PICO2_UART0_TX_GPIO                0u  /* Broche physique 1, sur les deux cartes. */
#define PICO2_UART0_RX_GPIO                1u
#define PICO2_ADC0_GPIO                    26u
#define PICO2_ADC1_GPIO                    27u
#define PICO2_ADC2_GPIO                    28u
#define PICO2_VSYS_ADC_CHANNEL             3u
#define RP2350A_ADC_TEMP_CHANNEL           4u

/* Identification et configuration système. La révision est dans CHIP_ID[31:28]. */
#define RP2350_SYSINFO_CHIP_ID             (RP2350_SYSINFO_BASE + 0x00u)
#define RP2350_SYSINFO_PLATFORM            (RP2350_SYSINFO_BASE + 0x08u)
#define RP2350_SYSINFO_CHIP_REV(mot)         RP2350_FIELD_GET(0xf0000000u, 28u, (mot))
#define RP2350_PSM_WDSEL                   (RP2350_PSM_BASE + 0x08u)
#define RP2350_PSM_DONE                    (RP2350_PSM_BASE + 0x0cu)
#define RP2350_BUSCTRL_BUS_PRIORITY         (RP2350_BUSCTRL_BASE + 0x00u)
#define RP2350_ACCESSCTRL_LOCK             (RP2350_ACCESSCTRL_BASE + 0x00u)
#define RP2350_ACCESSCTRL_GPIO_NSMASK0      (RP2350_ACCESSCTRL_BASE + 0x0cu)

/* Lever un reset en écrivant son masque dans ALIAS_CLR(RESET).
 * Attendre le même bit dans RESET_DONE avec un délai borné. */
#define RP2350_RESETS_RESET                (RP2350_RESETS_BASE + 0x00u)
#define RP2350_RESETS_WDSEL                (RP2350_RESETS_BASE + 0x04u)
#define RP2350_RESETS_RESET_DONE           (RP2350_RESETS_BASE + 0x08u)
#define RP2350_RESET_ADC                   RP2350_BIT(0)
#define RP2350_RESET_BUSCTRL               RP2350_BIT(1)
#define RP2350_RESET_DMA                   RP2350_BIT(2)
#define RP2350_RESET_HSTX                  RP2350_BIT(3)
#define RP2350_RESET_I2C0                  RP2350_BIT(4)
#define RP2350_RESET_I2C1                  RP2350_BIT(5)
#define RP2350_RESET_IO_BANK0              RP2350_BIT(6)
#define RP2350_RESET_IO_QSPI               RP2350_BIT(7)
#define RP2350_RESET_JTAG                  RP2350_BIT(8)
#define RP2350_RESET_PADS_BANK0            RP2350_BIT(9)
#define RP2350_RESET_PADS_QSPI             RP2350_BIT(10)
#define RP2350_RESET_PIO0                  RP2350_BIT(11)
#define RP2350_RESET_PIO1                  RP2350_BIT(12)
#define RP2350_RESET_PIO2                  RP2350_BIT(13)
#define RP2350_RESET_PLL_SYS               RP2350_BIT(14)
#define RP2350_RESET_PLL_USB               RP2350_BIT(15)
#define RP2350_RESET_PWM                   RP2350_BIT(16)
#define RP2350_RESET_SHA256                RP2350_BIT(17)
#define RP2350_RESET_SPI0                  RP2350_BIT(18)
#define RP2350_RESET_SPI1                  RP2350_BIT(19)
#define RP2350_RESET_SYSCFG                RP2350_BIT(20)
#define RP2350_RESET_SYSINFO               RP2350_BIT(21)
#define RP2350_RESET_TBMAN                 RP2350_BIT(22)
#define RP2350_RESET_TIMER0                RP2350_BIT(23)
#define RP2350_RESET_TIMER1                RP2350_BIT(24)
#define RP2350_RESET_TRNG                  RP2350_BIT(25)
#define RP2350_RESET_UART0                 RP2350_BIT(26)
#define RP2350_RESET_UART1                 RP2350_BIT(27)
#define RP2350_RESET_USB                   RP2350_BIT(28)

/* GPIO. IO_BANK0 choisit le signal interne. PADS_BANK0 règle le pad électrique.
 * Les numéros sur Pico 2/2 W restent sous 30. Vérifier la fonction disponible
 * pour chaque broche dans le tableau de multiplexage du RP2350. */
#define RP2350_GPIO_STATUS(gp)             (RP2350_IO_BANK0_BASE + 8u * (gp))
#define RP2350_GPIO_CTRL(gp)               (RP2350_GPIO_STATUS(gp) + 4u)
#define RP2350_PAD_GPIO(gp)                (RP2350_PADS_BANK0_BASE + 4u + 4u * (gp))
#define RP2350_GPIO_FUNCSEL_MASK            0x1fu
#define RP2350_FUNC_HSTX                    0u
#define RP2350_FUNC_SPI                     1u
#define RP2350_FUNC_UART                    2u
#define RP2350_FUNC_I2C                     3u
#define RP2350_FUNC_PWM                     4u
#define RP2350_FUNC_SIO                     5u
#define RP2350_FUNC_PIO0                    6u
#define RP2350_FUNC_PIO1                    7u
#define RP2350_FUNC_PIO2                    8u
#define RP2350_FUNC_NULL                   31u
#define RP2350_PAD_ISO                      RP2350_BIT(8) /* Isolation. */
#define RP2350_PAD_OD                       RP2350_BIT(7) /* Interdit la sortie. */
#define RP2350_PAD_IE                       RP2350_BIT(6) /* Active l'entrée numérique. */
#define RP2350_PAD_DRIVE_MASK              (3u << 4)       /* Courant de sortie nominal. */
#define RP2350_PAD_PUE                      RP2350_BIT(3) /* Tirage haut. */
#define RP2350_PAD_PDE                      RP2350_BIT(2) /* Tirage bas. */
#define RP2350_PAD_SCHMITT                  RP2350_BIT(1)
#define RP2350_PAD_SLEWFAST                 RP2350_BIT(0)
/* Huit broches par registre. Les fronts mémorisés sont W1C. Les niveaux
 * restent actifs tant que le signal physique se trouve au niveau demandé. */
#define RP2350_GPIO_INTR(groupe)           (RP2350_IO_BANK0_BASE + 0x230u + 4u * (groupe))
#define RP2350_GPIO_PROC_INTE(coeur, g)    (RP2350_IO_BANK0_BASE + 0x248u + 0x48u * (coeur) + 4u * (g))
#define RP2350_GPIO_PROC_INTF(coeur, g)    (RP2350_GPIO_PROC_INTE((coeur), (g)) + 0x18u)
#define RP2350_GPIO_PROC_INTS(coeur, g)    (RP2350_GPIO_PROC_INTE((coeur), (g)) + 0x30u)
#define RP2350_GPIO_IRQ_GROUP(gp)          ((gp) / 8u)
#define RP2350_GPIO_IRQ_FALLING(gp)        RP2350_BIT(4u * ((gp) % 8u) + 2u)
#define RP2350_GPIO_IRQ_RISING(gp)         RP2350_BIT(4u * ((gp) % 8u) + 3u)

/* SIO. Le masque GPIO vaut BIT(gp) pour les broches 0 à 31.
 * Le niveau se prépare dans OUT avant d'activer la sortie avec OE. */
#define RP2350_SIO_CPUID                   (RP2350_SIO_BASE + 0x00u)
#define RP2350_SIO_GPIO_IN                 (RP2350_SIO_BASE + 0x04u)
#define RP2350_SIO_GPIO_OUT                (RP2350_SIO_BASE + 0x10u)
#define RP2350_SIO_GPIO_OUT_SET            (RP2350_SIO_BASE + 0x18u)
#define RP2350_SIO_GPIO_OUT_CLR            (RP2350_SIO_BASE + 0x20u)
#define RP2350_SIO_GPIO_OUT_XOR            (RP2350_SIO_BASE + 0x28u)
#define RP2350_SIO_GPIO_OE                 (RP2350_SIO_BASE + 0x30u)
#define RP2350_SIO_GPIO_OE_SET             (RP2350_SIO_BASE + 0x38u)
#define RP2350_SIO_GPIO_OE_CLR             (RP2350_SIO_BASE + 0x40u)
#define RP2350_SIO_GPIO_OE_XOR             (RP2350_SIO_BASE + 0x48u)
#define RP2350_SIO_FIFO_ST                 (RP2350_SIO_BASE + 0x50u)
#define RP2350_SIO_FIFO_WR                 (RP2350_SIO_BASE + 0x54u)
#define RP2350_SIO_FIFO_RD                 (RP2350_SIO_BASE + 0x58u) /* Retire un mot. */
#define RP2350_SIO_FIFO_VLD                RP2350_BIT(0) /* Mot à lire. */
#define RP2350_SIO_FIFO_RDY                RP2350_BIT(1) /* Place en écriture. */
#define RP2350_SIO_INTERP_ACCUM0(n)        (RP2350_SIO_BASE + 0x80u + 0x40u * (n))
#define RP2350_SIO_SPINLOCK(n)             (RP2350_SIO_BASE + 0x100u + 4u * (n)) /* 0 à 31. */
#define RP2350_SIO_DOORBELL_OUT_SET        (RP2350_SIO_BASE + 0x180u)
#define RP2350_SIO_DOORBELL_IN_CLR         (RP2350_SIO_BASE + 0x18cu)
#define RP2350_SIO_MTIME_CTRL              (RP2350_SIO_BASE + 0x1a4u)
#define RP2350_SIO_MTIME                   (RP2350_SIO_BASE + 0x1b0u)

/* Horloges. Confirmer la source active avant de calculer un débit.
 * PLL emploie une des deux bases PLL_* selon la fréquence à produire. */
#define RP2350_XOSC_CTRL                   (RP2350_XOSC_BASE + 0x00u)
#define RP2350_XOSC_STATUS                 (RP2350_XOSC_BASE + 0x04u)
#define RP2350_XOSC_DORMANT                (RP2350_XOSC_BASE + 0x08u)
#define RP2350_XOSC_STARTUP                (RP2350_XOSC_BASE + 0x0cu)
#define RP2350_XOSC_STABLE                 RP2350_BIT(31)
#define RP2350_XOSC_ENABLE_VALUE           (0xfabu << 12)
#define RP2350_XOSC_FREQ_1_15_MHZ          0xaa0u
#define RP2350_PLL_CS(base)                ((base) + 0x00u)
#define RP2350_PLL_PWR(base)               ((base) + 0x04u)
#define RP2350_PLL_FBDIV_INT(base)         ((base) + 0x08u)
#define RP2350_PLL_PRIM(base)              ((base) + 0x0cu)
#define RP2350_PLL_LOCK                    RP2350_BIT(31)
#define RP2350_CLK_REF_CTRL                (RP2350_CLOCKS_BASE + 0x30u)
#define RP2350_CLK_REF_DIV                 (RP2350_CLOCKS_BASE + 0x34u)
#define RP2350_CLK_REF_SELECTED            (RP2350_CLOCKS_BASE + 0x38u)
#define RP2350_CLK_SYS_CTRL                (RP2350_CLOCKS_BASE + 0x3cu)
#define RP2350_CLK_SYS_DIV                 (RP2350_CLOCKS_BASE + 0x40u)
#define RP2350_CLK_SYS_SELECTED            (RP2350_CLOCKS_BASE + 0x44u)
#define RP2350_CLK_PERI_CTRL               (RP2350_CLOCKS_BASE + 0x48u)
#define RP2350_CLK_PERI_DIV                (RP2350_CLOCKS_BASE + 0x4cu)
#define RP2350_CLK_HSTX_CTRL               (RP2350_CLOCKS_BASE + 0x54u)
#define RP2350_CLK_USB_CTRL                (RP2350_CLOCKS_BASE + 0x60u)
#define RP2350_CLK_USB_DIV                 (RP2350_CLOCKS_BASE + 0x64u)
#define RP2350_CLK_ADC_CTRL                (RP2350_CLOCKS_BASE + 0x6cu)
#define RP2350_CLK_ADC_DIV                 (RP2350_CLOCKS_BASE + 0x70u)
#define RP2350_CLK_FC0_SRC                 (RP2350_CLOCKS_BASE + 0xa0u)
#define RP2350_CLK_REF_SRC_XOSC            2u
#define RP2350_CLK_PERI_AUXSRC_XOSC        (4u << 5)
#define RP2350_CLK_CTRL_ENABLE             RP2350_BIT(11)
#define RP2350_CLK_CTRL_ENABLED            RP2350_BIT(28)

/* Timers à compteur 64 bits. Lire TIMELR avant TIMEHR pour figer le mot haut.
 * ALARM(n) prend un numéro de 0 à 3. INTR est W1C. ARMED écrit avec un 1
 * désarme l'alarme correspondante. SOURCE vaut 0 pour TICKS, 1 pour clk_sys. */
#define RP2350_TIMER_TIMEHW(base)          ((base) + 0x00u)
#define RP2350_TIMER_TIMELW(base)          ((base) + 0x04u)
#define RP2350_TIMER_TIMEHR(base)          ((base) + 0x08u)
#define RP2350_TIMER_TIMELR(base)          ((base) + 0x0cu)
#define RP2350_TIMER_ALARM(base, n)        ((base) + 0x10u + 4u * (n))
#define RP2350_TIMER_ARMED(base)           ((base) + 0x20u)
#define RP2350_TIMER_RAWH(base)            ((base) + 0x24u)
#define RP2350_TIMER_RAWL(base)            ((base) + 0x28u)
#define RP2350_TIMER_DBGPAUSE(base)        ((base) + 0x2cu)
#define RP2350_TIMER_PAUSE(base)           ((base) + 0x30u)
#define RP2350_TIMER_LOCKED(base)          ((base) + 0x34u)
#define RP2350_TIMER_SOURCE(base)          ((base) + 0x38u)
#define RP2350_TIMER_INTR(base)            ((base) + 0x3cu)
#define RP2350_TIMER_INTE(base)            ((base) + 0x40u)
#define RP2350_TIMER_INTF(base)            ((base) + 0x44u)
#define RP2350_TIMER_INTS(base)            ((base) + 0x48u)
#define RP2350_TICK_PROC0                  0u
#define RP2350_TICK_PROC1                  1u
#define RP2350_TICK_TIMER0                 2u
#define RP2350_TICK_TIMER1                 3u
#define RP2350_TICK_WATCHDOG               4u
#define RP2350_TICK_RISCV                  5u
#define RP2350_TICK_CTRL(n)                (RP2350_TICKS_BASE + 12u * (n))
#define RP2350_TICK_CYCLES(n)              (RP2350_TICK_CTRL(n) + 4u)
#define RP2350_TICK_COUNT(n)               (RP2350_TICK_CTRL(n) + 8u)
#define RP2350_TICK_ENABLE                 RP2350_BIT(0)
#define RP2350_TICK_RUNNING                RP2350_BIT(1)

/* UART. L'écriture de DR place une donnée dans la FIFO TX. Lire DR retire
 * une donnée RX. Contrôler FR avant l'accès. Régler LCR_H après IBRD/FBRD. */
#define RP2350_UART_DR(base)               ((base) + 0x00u)
#define RP2350_UART_RSR_ECR(base)          ((base) + 0x04u)
#define RP2350_UART_FR(base)               ((base) + 0x18u)
#define RP2350_UART_IBRD(base)             ((base) + 0x24u)
#define RP2350_UART_FBRD(base)             ((base) + 0x28u)
#define RP2350_UART_LCR_H(base)            ((base) + 0x2cu)
#define RP2350_UART_CR(base)               ((base) + 0x30u)
#define RP2350_UART_IFLS(base)             ((base) + 0x34u)
#define RP2350_UART_IMSC(base)             ((base) + 0x38u)
#define RP2350_UART_RIS(base)              ((base) + 0x3cu)
#define RP2350_UART_MIS(base)              ((base) + 0x40u)
#define RP2350_UART_ICR(base)              ((base) + 0x44u)
#define RP2350_UART_DMACR(base)            ((base) + 0x48u)
#define RP2350_UART_BUSY                   RP2350_BIT(3)
#define RP2350_UART_RXFE                   RP2350_BIT(4)
#define RP2350_UART_TXFF                   RP2350_BIT(5)
#define RP2350_UART_FIFO_ENABLE            RP2350_BIT(4) /* LCR_H. */
#define RP2350_UART_WORD_8_BITS            (3u << 5)       /* LCR_H. */
#define RP2350_UART_ENABLE                 RP2350_BIT(0) /* CR. */
#define RP2350_UART_TX_ENABLE              RP2350_BIT(8) /* CR. */
#define RP2350_UART_RX_ENABLE              RP2350_BIT(9) /* CR. */

/* SPI. Les deux FIFO doivent être surveillées pendant un échange. Le débit
 * dépend du diviseur pair CPSR, de SCR dans CR0 et de clk_peri. */
#define RP2350_SPI_CR0(base)               ((base) + 0x00u)
#define RP2350_SPI_CR1(base)               ((base) + 0x04u)
#define RP2350_SPI_DR(base)                ((base) + 0x08u)
#define RP2350_SPI_SR(base)                ((base) + 0x0cu)
#define RP2350_SPI_CPSR(base)              ((base) + 0x10u)
#define RP2350_SPI_IMSC(base)              ((base) + 0x14u)
#define RP2350_SPI_RIS(base)               ((base) + 0x18u)
#define RP2350_SPI_MIS(base)               ((base) + 0x1cu)
#define RP2350_SPI_ICR(base)               ((base) + 0x20u)
#define RP2350_SPI_DMACR(base)             ((base) + 0x24u)
#define RP2350_SPI_LOOPBACK                RP2350_BIT(0) /* CR1. */
#define RP2350_SPI_ENABLE                  RP2350_BIT(1) /* CR1.SSE. */
#define RP2350_SPI_TX_NOT_FULL             RP2350_BIT(1) /* SR.TNF. */
#define RP2350_SPI_RX_NOT_EMPTY            RP2350_BIT(2) /* SR.RNE. */
#define RP2350_SPI_BUSY                    RP2350_BIT(4) /* SR.BSY. */

/* I²C. Renseigner une adresse cible sur 7 bits sans la décaler.
 * Régler la vitesse avec le bloc désactivé. Un accès à DATA_CMD peut
 * lancer une commande ou retirer une donnée. CLR_TX_ABRT s'acquitte en lecture. */
#define RP2350_I2C_CON(base)               ((base) + 0x00u)
#define RP2350_I2C_TAR(base)               ((base) + 0x04u)
#define RP2350_I2C_SAR(base)               ((base) + 0x08u)
#define RP2350_I2C_DATA_CMD(base)          ((base) + 0x10u)
#define RP2350_I2C_SS_SCL_HCNT(base)       ((base) + 0x14u)
#define RP2350_I2C_SS_SCL_LCNT(base)       ((base) + 0x18u)
#define RP2350_I2C_FS_SCL_HCNT(base)       ((base) + 0x1cu)
#define RP2350_I2C_FS_SCL_LCNT(base)       ((base) + 0x20u)
#define RP2350_I2C_INTR_STAT(base)         ((base) + 0x2cu)
#define RP2350_I2C_INTR_MASK(base)         ((base) + 0x30u)
#define RP2350_I2C_RAW_INTR_STAT(base)     ((base) + 0x34u)
#define RP2350_I2C_CLR_TX_ABRT(base)       ((base) + 0x54u)
#define RP2350_I2C_ENABLE(base)            ((base) + 0x6cu)
#define RP2350_I2C_STATUS(base)            ((base) + 0x70u)
#define RP2350_I2C_TXFLR(base)             ((base) + 0x74u)
#define RP2350_I2C_RXFLR(base)             ((base) + 0x78u)
#define RP2350_I2C_TX_ABRT_SOURCE(base)    ((base) + 0x80u)
#define RP2350_I2C_DMA_CR(base)            ((base) + 0x88u)
#define RP2350_I2C_ENABLE_STATUS(base)     ((base) + 0x9cu)
#define RP2350_I2C_CMD_READ                RP2350_BIT(8)
#define RP2350_I2C_CMD_STOP                RP2350_BIT(9)
#define RP2350_I2C_CMD_RESTART             RP2350_BIT(10)
#define RP2350_I2C_TX_NOT_FULL             RP2350_BIT(1) /* STATUS.TFNF. */
#define RP2350_I2C_RX_NOT_EMPTY            RP2350_BIT(3) /* STATUS.RFNE. */
#define RP2350_I2C_TX_ABRT                 RP2350_BIT(6) /* RAW_INTR_STAT. */

/* ADC, conversion analogique-numérique. Le résultat a 12 bits.
 * Le démarrage et le choix du canal se configurent dans CS. FIFO est
 * une lecture destructrice. Le canal de température exige CS.TS_EN. */
#define RP2350_ADC_CS                      (RP2350_ADC_BASE + 0x00u)
#define RP2350_ADC_RESULT                  (RP2350_ADC_BASE + 0x04u)
#define RP2350_ADC_FCS                     (RP2350_ADC_BASE + 0x08u)
#define RP2350_ADC_FIFO                    (RP2350_ADC_BASE + 0x0cu)
#define RP2350_ADC_DIV                     (RP2350_ADC_BASE + 0x10u)
#define RP2350_ADC_INTR                    (RP2350_ADC_BASE + 0x14u)
#define RP2350_ADC_INTE                    (RP2350_ADC_BASE + 0x18u)
#define RP2350_ADC_INTF                    (RP2350_ADC_BASE + 0x1cu)
#define RP2350_ADC_INTS                    (RP2350_ADC_BASE + 0x20u)
#define RP2350_ADC_ENABLE                  RP2350_BIT(0)
#define RP2350_ADC_TS_ENABLE               RP2350_BIT(1)
#define RP2350_ADC_START_ONCE              RP2350_BIT(2)
#define RP2350_ADC_START_MANY              RP2350_BIT(3)
#define RP2350_ADC_READY                   RP2350_BIT(8)
#define RP2350_ADC_ERROR                   RP2350_BIT(9)
#define RP2350_ADC_AINSEL_MASK             (0xfu << 12)
#define RP2350_ADC_AINSEL(n)               RP2350_FIELD_PREP(RP2350_ADC_AINSEL_MASK, 12u, (n))
#define RP2350_ADC_RESULT_MASK             0xfffu

/* PWM. Les tranches 0 à 11 contiennent chacune deux comparateurs A/B.
 * CC contient A dans les 16 bits bas et B dans les 16 bits hauts. Les deux sorties
 * partagent le diviseur, le compteur et le seuil TOP. */
#define RP2350_PWM_CHANNEL(n)              (RP2350_PWM_BASE + 0x14u * (n))
#define RP2350_PWM_CSR(n)                  (RP2350_PWM_CHANNEL(n) + 0x00u)
#define RP2350_PWM_DIV(n)                  (RP2350_PWM_CHANNEL(n) + 0x04u)
#define RP2350_PWM_CTR(n)                  (RP2350_PWM_CHANNEL(n) + 0x08u)
#define RP2350_PWM_CC(n)                   (RP2350_PWM_CHANNEL(n) + 0x0cu)
#define RP2350_PWM_TOP(n)                  (RP2350_PWM_CHANNEL(n) + 0x10u)
#define RP2350_PWM_EN                      (RP2350_PWM_BASE + 0xf0u)
#define RP2350_PWM_INTR                    (RP2350_PWM_BASE + 0xf4u)
#define RP2350_PWM_IRQ_INTE(n)             (RP2350_PWM_BASE + 0xf8u + 0x0cu * (n))
#define RP2350_PWM_IRQ_INTF(n)             (RP2350_PWM_IRQ_INTE(n) + 4u)
#define RP2350_PWM_IRQ_INTS(n)             (RP2350_PWM_IRQ_INTE(n) + 8u)
#define RP2350_PWM_CSR_EN                  RP2350_BIT(0)
#define RP2350_PWM_CC_PACK(a, b)           (((uint32_t)(a) & 0xffffu) | (((uint32_t)(b) & 0xffffu) << 16))

/* DMA. Les canaux 0 à 15 ont un pas de 0x40. TRANS_COUNT compte des
 * transferts, pas des octets. Le mode est dans [31:28], le compte dans [27:0].
 * Écrire CTRL_TRIG en dernier. Attendre BUSY=0 avant de réutiliser les
 * tampons. DREQ cadence un transfert depuis ou vers la FIFO d'un bloc. */
#define RP2350_DMA_CHANNEL(n)              (RP2350_DMA_BASE + 0x40u * (n))
#define RP2350_DMA_READ_ADDR(n)            (RP2350_DMA_CHANNEL(n) + 0x00u)
#define RP2350_DMA_WRITE_ADDR(n)           (RP2350_DMA_CHANNEL(n) + 0x04u)
#define RP2350_DMA_TRANS_COUNT(n)          (RP2350_DMA_CHANNEL(n) + 0x08u)
#define RP2350_DMA_CTRL_TRIG(n)            (RP2350_DMA_CHANNEL(n) + 0x0cu)
#define RP2350_DMA_INTR                    (RP2350_DMA_BASE + 0x400u)
#define RP2350_DMA_IRQ_INTE(n)             (RP2350_DMA_BASE + 0x404u + 0x10u * (n))
#define RP2350_DMA_IRQ_INTF(n)             (RP2350_DMA_IRQ_INTE(n) + 4u)
#define RP2350_DMA_IRQ_INTS(n)             (RP2350_DMA_IRQ_INTE(n) + 8u)
#define RP2350_DMA_CHAN_ABORT              (RP2350_DMA_BASE + 0x464u)
#define RP2350_DMA_COUNT_MASK              0x0fffffffu
#define RP2350_DMA_CTRL_EN                 RP2350_BIT(0)
#define RP2350_DMA_SIZE_8_BITS             (0u << 2)
#define RP2350_DMA_SIZE_16_BITS            (1u << 2)
#define RP2350_DMA_SIZE_32_BITS            (2u << 2)
#define RP2350_DMA_INCR_READ               RP2350_BIT(4)
#define RP2350_DMA_INCR_WRITE              RP2350_BIT(6)
#define RP2350_DMA_CHAIN_TO(n)             RP2350_FIELD_PREP(0x0001e000u, 13u, (n))
#define RP2350_DMA_TREQ_SEL(n)             RP2350_FIELD_PREP(0x007e0000u, 17u, (n))
#define RP2350_DMA_BUSY                    RP2350_BIT(26)
#define RP2350_DMA_AHB_ERROR               RP2350_BIT(31)
/* pio de 0 à 2, sm de 0 à 3. TX et RX ont des requêtes distinctes. */
#define RP2350_DREQ_PIO_TX(pio, sm)        (8u * (pio) + (sm))
#define RP2350_DREQ_PIO_RX(pio, sm)        (8u * (pio) + 4u + (sm))
#define RP2350_DREQ_SPI0_TX                24u
#define RP2350_DREQ_SPI0_RX                25u
#define RP2350_DREQ_SPI1_TX                26u
#define RP2350_DREQ_SPI1_RX                27u
#define RP2350_DREQ_UART0_TX               28u
#define RP2350_DREQ_UART0_RX               29u
#define RP2350_DREQ_UART1_TX               30u
#define RP2350_DREQ_UART1_RX               31u
#define RP2350_DREQ_PWM_WRAP(n)            (32u + (n)) /* Tranche 0 à 11. */
#define RP2350_DREQ_I2C0_TX                44u
#define RP2350_DREQ_I2C0_RX                45u
#define RP2350_DREQ_I2C1_TX                46u
#define RP2350_DREQ_I2C1_RX                47u
#define RP2350_DREQ_ADC                    48u
#define RP2350_DREQ_XIP_STREAM             49u
#define RP2350_DREQ_XIP_QMI_TX             50u
#define RP2350_DREQ_XIP_QMI_RX             51u
#define RP2350_DREQ_HSTX                   52u
#define RP2350_DREQ_CORESIGHT              53u
#define RP2350_DREQ_SHA256                 54u
#define RP2350_DREQ_DMA_TIMER(n)           (59u + (n)) /* Timer DMA 0 à 3. */
#define RP2350_DREQ_PERMANENT              63u

/* PIO. Trois blocs possèdent chacun quatre machines d'état et une mémoire
 * de 32 instructions partagée. Réserver les emplacements. INSTR_MEM est
 * en écriture seule. Écrire SM_INSTR exécute immédiatement une instruction. */
#define RP2350_PIO_CTRL(base)              ((base) + 0x00u)
#define RP2350_PIO_FSTAT(base)             ((base) + 0x04u)
#define RP2350_PIO_FDEBUG(base)            ((base) + 0x08u)
#define RP2350_PIO_FLEVEL(base)            ((base) + 0x0cu)
#define RP2350_PIO_TXF(base, sm)           ((base) + 0x10u + 4u * (sm))
#define RP2350_PIO_RXF(base, sm)           ((base) + 0x20u + 4u * (sm))
#define RP2350_PIO_IRQ(base)               ((base) + 0x30u)
#define RP2350_PIO_IRQ_FORCE(base)         ((base) + 0x34u)
#define RP2350_PIO_INPUT_SYNC_BYPASS(base) ((base) + 0x38u)
#define RP2350_PIO_INSTR_MEM(base, n)      ((base) + 0x48u + 4u * (n))
#define RP2350_PIO_SM(base, sm)            ((base) + 0xc8u + 0x18u * (sm))
#define RP2350_PIO_SM_CLKDIV(base, sm)      RP2350_PIO_SM((base), (sm))
#define RP2350_PIO_SM_EXECCTRL(base, sm)   (RP2350_PIO_SM((base), (sm)) + 0x04u)
#define RP2350_PIO_SM_SHIFTCTRL(base, sm)  (RP2350_PIO_SM((base), (sm)) + 0x08u)
#define RP2350_PIO_SM_ADDR(base, sm)       (RP2350_PIO_SM((base), (sm)) + 0x0cu)
#define RP2350_PIO_SM_INSTR(base, sm)      (RP2350_PIO_SM((base), (sm)) + 0x10u)
#define RP2350_PIO_SM_PINCTRL(base, sm)    (RP2350_PIO_SM((base), (sm)) + 0x14u)
#define RP2350_PIO_GPIOBASE(base)          ((base) + 0x168u)
#define RP2350_PIO_INTR(base)              ((base) + 0x16cu)
#define RP2350_PIO_IRQ_INTE(base, n)       ((base) + 0x170u + 0x0cu * (n))
#define RP2350_PIO_IRQ_INTF(base, n)       (RP2350_PIO_IRQ_INTE((base), (n)) + 4u)
#define RP2350_PIO_IRQ_INTS(base, n)       (RP2350_PIO_IRQ_INTE((base), (n)) + 8u)
#define RP2350_PIO_SM_ENABLE(sm)           RP2350_BIT(sm)
#define RP2350_PIO_TX_FULL(sm)             RP2350_BIT(16u + (sm))
#define RP2350_PIO_RX_EMPTY(sm)            RP2350_BIT(8u + (sm))

/* USB. Le contrôleur traite le bus. La DPRAM stocke ses descripteurs de
 * tampon et données. Vérifier la propriété et l'état d'un tampon avant
 * de l'écrire. L'USB Full-Speed demande une horloge dédiée de 48 MHz. */
#define RP2350_USB_SETUP_WORD(n)          (RP2350_USB_DPRAM_BASE + 4u * (n)) /* 0 ou 1. */
#define RP2350_USB_ADDR_ENDP               (RP2350_USB_REGS_BASE + 0x00u)
#define RP2350_USB_MAIN_CTRL               (RP2350_USB_REGS_BASE + 0x40u)
#define RP2350_USB_SIE_CTRL                (RP2350_USB_REGS_BASE + 0x4cu)
#define RP2350_USB_SIE_STATUS              (RP2350_USB_REGS_BASE + 0x50u)
#define RP2350_USB_BUFF_STATUS             (RP2350_USB_REGS_BASE + 0x58u)
#define RP2350_USB_PWR                     (RP2350_USB_REGS_BASE + 0x78u)
#define RP2350_USB_INTR                    (RP2350_USB_REGS_BASE + 0x8cu)
#define RP2350_USB_INTE                    (RP2350_USB_REGS_BASE + 0x90u)
#define RP2350_USB_INTF                    (RP2350_USB_REGS_BASE + 0x94u)
#define RP2350_USB_INTS                    (RP2350_USB_REGS_BASE + 0x98u)

/* HSTX, sérialiseur numérique. Le mux HSTX est disponible sur GP12 à GP19.
 * STAT.FULL indique quand attendre avant de remplir la FIFO. */
#define RP2350_HSTX_CSR                    (RP2350_HSTX_CTRL_BASE + 0x00u)
#define RP2350_HSTX_BIT_CTRL(n)            (RP2350_HSTX_CTRL_BASE + 4u + 4u * (n)) /* 0 à 7. */
#define RP2350_HSTX_CSR_EN                 RP2350_BIT(0)
#define RP2350_HSTX_FIFO_STAT              (RP2350_HSTX_FIFO_BASE + 0x00u)
#define RP2350_HSTX_FIFO_DATA              (RP2350_HSTX_FIFO_BASE + 0x04u)
#define RP2350_HSTX_FIFO_FULL              RP2350_BIT(8)

/* Watchdog. Configurer d'abord son tick et les domaines de reset dans PSM.
 * LOAD recharge le décompte. Une expiration peut provoquer un redémarrage. */
#define RP2350_WATCHDOG_CTRL               (RP2350_WATCHDOG_BASE + 0x00u)
#define RP2350_WATCHDOG_LOAD               (RP2350_WATCHDOG_BASE + 0x04u)
#define RP2350_WATCHDOG_REASON             (RP2350_WATCHDOG_BASE + 0x08u)
#define RP2350_WATCHDOG_SCRATCH(n)         (RP2350_WATCHDOG_BASE + 0x0cu + 4u * (n))
#define RP2350_WATCHDOG_ENABLE             RP2350_BIT(30)
#define RP2350_WATCHDOG_LOAD_MASK          0x00ffffffu
#define RP2350_WATCHDOG_REASON_TIMER       RP2350_BIT(0)
#define RP2350_WATCHDOG_REASON_FORCE       RP2350_BIT(1)

/* POWMAN et minuterie AON (domaine toujours alimenté). Les écritures
 * de configuration exigent la séquence et le mot de passe du fabricant. */
#define RP2350_POWMAN_CHIP_RESET           (RP2350_POWMAN_BASE + 0x2cu)
#define RP2350_POWMAN_STATE                (RP2350_POWMAN_BASE + 0x38u)
#define RP2350_POWMAN_TIME_UPPER           (RP2350_POWMAN_BASE + 0x70u)
#define RP2350_POWMAN_TIME_LOWER           (RP2350_POWMAN_BASE + 0x74u)
#define RP2350_POWMAN_TIMER                (RP2350_POWMAN_BASE + 0x88u)

/* QMI contrôle la Flash externe. Utiliser la documentation de la ROM
 * avant d'écrire en Flash. Le code, les IRQ, le DMA et l'autre cœur ne
 * doivent pas accéder à XIP pendant une transaction Flash incompatible. */
#define RP2350_XIP_CTRL                    (RP2350_XIP_CTRL_BASE + 0x00u)
#define RP2350_QMI_DIRECT_CSR              (RP2350_XIP_QMI_BASE + 0x00u)
#define RP2350_QMI_DIRECT_TX               (RP2350_XIP_QMI_BASE + 0x04u)
#define RP2350_QMI_DIRECT_RX               (RP2350_XIP_QMI_BASE + 0x08u)
#define RP2350_QMI_M0_TIMING               (RP2350_XIP_QMI_BASE + 0x0cu)
#define RP2350_QMI_M0_RFMT                 (RP2350_XIP_QMI_BASE + 0x10u)
#define RP2350_QMI_M0_RCMD                 (RP2350_XIP_QMI_BASE + 0x14u)

/* Aléa matériel et hachage. Lire les blocs TRNG selon les tests de santé
 * du fabricant. Fournir explicitement le padding et l'ordre des octets
 * au bloc SHA-256. Les huit mots SUM ne sont valides qu'après SUM_VALID. */
#define RP2350_TRNG_RNG_IMR                (RP2350_TRNG_BASE + 0x100u)
#define RP2350_TRNG_RNG_ISR                (RP2350_TRNG_BASE + 0x104u)
#define RP2350_TRNG_VALID                  (RP2350_TRNG_BASE + 0x110u)
#define RP2350_TRNG_EHR_DATA(n)            (RP2350_TRNG_BASE + 0x114u + 4u * (n)) /* 0 à 5. */
#define RP2350_TRNG_RND_SOURCE_ENABLE      (RP2350_TRNG_BASE + 0x12cu)
#define RP2350_TRNG_EHR_VALID              RP2350_BIT(0)
#define RP2350_SHA256_CSR                  (RP2350_SHA256_BASE + 0x00u)
#define RP2350_SHA256_WDATA                (RP2350_SHA256_BASE + 0x04u)
#define RP2350_SHA256_SUM(n)               (RP2350_SHA256_BASE + 0x08u + 4u * (n)) /* 0 à 7. */
#define RP2350_SHA256_START                RP2350_BIT(0)
#define RP2350_SHA256_WDATA_READY          RP2350_BIT(1)
#define RP2350_SHA256_SUM_VALID            RP2350_BIT(2)
#define RP2350_SHA256_BSWAP                RP2350_BIT(12)

/* OTP est programmable une seule fois. La cartographie expose aussi
 * des vues et des verrous à employer uniquement selon la procédure ROM. */
#define RP2350_OTP_SW_LOCK(n)              (RP2350_OTP_BASE + 4u * (n))

/* Cortex-M33. Deux banques NVIC par cœur pour les IRQ 0 à 51.
 * ISER/ICER/ICPR prennent un masque écrit directement. VTOR pointe
 * sur la table de vecteurs du domaine courant. Vérifier le domaine
 * Secure/Non-secure avant d'agir sur les exceptions et leur priorité. */
#define RP2350_SYST_CSR                    (RP2350_PPB_BASE + 0xe010u)
#define RP2350_SYST_RVR                    (RP2350_PPB_BASE + 0xe014u)
#define RP2350_SYST_CVR                    (RP2350_PPB_BASE + 0xe018u)
#define RP2350_SYST_ENABLE                 RP2350_BIT(0)
#define RP2350_SYST_TICKINT                RP2350_BIT(1)
#define RP2350_SYST_CLKSOURCE_CPU          RP2350_BIT(2)
#define RP2350_SYST_COUNTFLAG              RP2350_BIT(16) /* Effacé en lisant CSR. */
#define RP2350_NVIC_ISER(banque)          (RP2350_PPB_BASE + 0xe100u + 4u * (banque))
#define RP2350_NVIC_ICER(banque)          (RP2350_PPB_BASE + 0xe180u + 4u * (banque))
#define RP2350_NVIC_ISPR(banque)          (RP2350_PPB_BASE + 0xe200u + 4u * (banque))
#define RP2350_NVIC_ICPR(banque)          (RP2350_PPB_BASE + 0xe280u + 4u * (banque))
#define RP2350_NVIC_IABR(banque)          (RP2350_PPB_BASE + 0xe300u + 4u * (banque))
#define RP2350_NVIC_ITNS(banque)          (RP2350_PPB_BASE + 0xe380u + 4u * (banque))
#define RP2350_NVIC_IPR_WORD(n)           (RP2350_PPB_BASE + 0xe400u + 4u * (n))
#define RP2350_NVIC_IRQ_BANK(irq)          ((irq) / 32u)
#define RP2350_NVIC_IRQ_MASK(irq)          RP2350_BIT((irq) % 32u)
#define RP2350_SCB_CPUID                   (RP2350_PPB_BASE + 0xed00u)
#define RP2350_SCB_ICSR                    (RP2350_PPB_BASE + 0xed04u)
#define RP2350_SCB_VTOR                    (RP2350_PPB_BASE + 0xed08u)
#define RP2350_SCB_AIRCR                   (RP2350_PPB_BASE + 0xed0cu)
#define RP2350_SCB_SCR                     (RP2350_PPB_BASE + 0xed10u)
#define RP2350_SCB_SHCSR                   (RP2350_PPB_BASE + 0xed24u)
#define RP2350_SCB_CFSR                    (RP2350_PPB_BASE + 0xed28u)
#define RP2350_SCB_HFSR                    (RP2350_PPB_BASE + 0xed2cu)
#define RP2350_SCB_MMFAR                   (RP2350_PPB_BASE + 0xed34u)
#define RP2350_SCB_BFAR                    (RP2350_PPB_BASE + 0xed38u)
#define RP2350_SCB_CPACR                   (RP2350_PPB_BASE + 0xed88u)
#define RP2350_MPU_TYPE                    (RP2350_PPB_BASE + 0xed90u)
#define RP2350_MPU_CTRL                    (RP2350_PPB_BASE + 0xed94u)
#define RP2350_SAU_CTRL                    (RP2350_PPB_BASE + 0xedd0u) /* Vue Secure. */

/* Numéros IRQ externes. L'indice correspondant dans les vecteurs Arm
 * vaut numéro + 16. Raccorder la bonne autorisation du périphérique. */
#define RP2350_IRQ_TIMER0_ALARM(n)         (0u + (n))
#define RP2350_IRQ_TIMER1_ALARM(n)         (4u + (n))
#define RP2350_IRQ_PWM_WRAP(n)             (8u + (n))
#define RP2350_IRQ_DMA(n)                  (10u + (n))
#define RP2350_IRQ_USB                     14u
#define RP2350_IRQ_PIO(pio, ligne)         (15u + 2u * (pio) + (ligne))
#define RP2350_IRQ_IO_BANK0                21u
#define RP2350_IRQ_IO_BANK0_NS             22u
#define RP2350_IRQ_IO_QSPI                 23u
#define RP2350_IRQ_IO_QSPI_NS              24u
#define RP2350_IRQ_SIO_FIFO               25u
#define RP2350_IRQ_SIO_DOORBELL           26u
#define RP2350_IRQ_SIO_FIFO_NS            27u
#define RP2350_IRQ_SIO_DOORBELL_NS        28u
#define RP2350_IRQ_SIO_MTIMECMP           29u
#define RP2350_IRQ_CLOCKS                  30u
#define RP2350_IRQ_SPI0                    31u
#define RP2350_IRQ_SPI1                    32u
#define RP2350_IRQ_UART0                   33u
#define RP2350_IRQ_UART1                   34u
#define RP2350_IRQ_ADC_FIFO                35u
#define RP2350_IRQ_I2C0                    36u
#define RP2350_IRQ_I2C1                    37u
#define RP2350_IRQ_OTP                     38u
#define RP2350_IRQ_TRNG                    39u
#define RP2350_IRQ_CORE0_CTI               40u
#define RP2350_IRQ_CORE1_CTI               41u
#define RP2350_IRQ_PLL_SYS                 42u
#define RP2350_IRQ_PLL_USB                 43u
#define RP2350_IRQ_POWMAN_POWER            44u
#define RP2350_IRQ_POWMAN_TIMER            45u
#define RP2350_IRQ_SLOTS                   52u  /* 46 à 51 sont réservés. */
#define RP2350_IRQ_VECTOR(irq)            (16u + (irq))

#endif /* PICODEBUGGER_RP2350_REGS_H */
