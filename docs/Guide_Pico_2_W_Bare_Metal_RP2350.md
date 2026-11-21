# Raspberry Pi Pico 2 W — Guide bare metal pour développeur système

**Cible.** Raspberry Pi Pico 2 W (RP2350A), cœur 0 ARM Cortex-M33, C11 bare metal. Les exercices sur GP15, UART, timers et les contrôleurs du RP2350 s'appliquent aussi à la **Pico 2 sans W**. Les chapitres radio et le brochage interne de la LED distinguent les variantes.

**Objectif.** Comprendre le chemin des octets et des événements entre le processeur, la mémoire, les broches, les interruptions et la radio. Le parcours commence par un ELF en SRAM qui commande une LED externe. Il progresse ensuite du premier pilote jusqu'aux mécanismes utiles à un petit noyau.

| Étape | Ce que tu sauras vérifier |
|---|---|
| [1. Lire la carte](#1-matériel--ce-qui-est-réellement-sur-la-pico-2-w) et [l'espace mémoire](#2-carte-mémoire--ce-quil-faut-mémoriser) | Distinguer une broche, un registre et une adresse de RAM |
| [2. Compiler et allumer une LED](#4-ton-premier-programme--led-externe-sur-gp15-en-ram) | Retrouver le C, le startup, l'ELF et le premier accès MMIO |
| [3. Ajouter le temps et les interruptions](#8-timer-système--lindispensable-pour-le-scheduler) | Faire varier un signal à intervalle mesuré et acquitter une alarme |
| [4. Construire des pilotes](#18-fiches-périphériques--ce-que-cest-comment-le-démarrer-où-lire-les-registres) | Relier une horloge, un reset, un mux, des registres et un résultat observable |
| [5. Aborder les fonctions avancées](#20-glossaire) | Retrouver les termes, les contraintes du boot et les références par bloc |

**Matériel et outils de départ.** Une Pico 2 ou 2 W, une LED externe, une résistance de 330 Ω, une masse commune, un câble USB de données et une sonde SWD pour l'exercice SRAM. Sur le PC, installer GNU Make et la chaîne GNU Arm bare metal. `arm-none-eabi-gcc --version` et `arm-none-eabi-readelf --version` doivent répondre. PicoDebugger peut utiliser la Raspberry Pi Debug Probe avec OpenOCD et GDB ARM. [Connexion et dépannage SWD](HARDWARE_DEBUG.md)

**État de validation.** Les deux ELF de l'exemple sont compilés et inspectés avec la chaîne GNU Arm. Les fiches donnent des fragments C annotés et leurs préconditions. La conversion UF2, le montage physique, les pilotes avancés, le boot Flash et la radio demandent une validation complémentaire. Les définitions de registres du fabricant servent de référence, sans dépendance au Pico SDK dans le firmware.

## 0. Modèle mental : le Pico vu d'un développeur logiciel

Le microcontrôleur ne possède ni BIOS PC, ni MMU généraliste, ni espace d'adressage virtuel par processus. Dans le cas simple, le code en mode privilégié lit et écrit **des adresses physiques** : une partie désigne de la SRAM, une autre des registres de périphériques (*MMIO*). Un contrôleur de reset, un multiplexeur de broches et des horloges conditionnent l'accès aux périphériques.

```
PC/CPU Cortex-M33
    │ instructions : ROM / SRAM / Flash XIP
    ├─ load/store ─► SRAM                        (variables / piles / heap)
    ├─ load/store ─► MMIO                        (GPIO, UART, DMA, timer...)
    ├─ exceptions ◄── NVIC ◄── contrôleurs IRQ
    └─ bus PIO/gSPI ─► CYW43439 (firmware radio) ─► Wi-Fi / Bluetooth

Débogueur SWD ─► DAP/cores ─► RAM, registres CPU, points d'arrêt, exécution
```

Règle d'or : **écrire dans le registre de configuration du périphérique ne suffit pas**. Dans l'ordre, confirmer alimentation → horloges → sortie de reset → autorisations d'accès → multiplexage GPIO/pads → configuration → activation → statut/IRQ.

## 1. Matériel : ce qui est réellement sur la Pico 2 W

- **RP2350A :** deux cœurs utilisables dans une architecture **Arm Cortex-M33** *ou* deux cœurs **RISC-V Hazard3** ; le choix de l'architecture concerne le mode de démarrage. Ce guide cible M33, cœur 0, **un seul cœur actif** pour commencer.
- **Mémoire interne :** **520 Kio de SRAM** ; **Flash externe QSPI de 4 Mio** sur la carte, exposée en lecture/exécution via XIP lorsqu'elle est initialisée.
- **Périphériques principaux :** GPIO avec mux/pads, SIO, 2 UART, 2 SPI, 2 I²C, 2 timers matériels à 4 alarmes chacun, 12 tranches PWM dans la puce, ADC 12 bits, 3 blocs PIO de 4 machines d'état, DMA à 16 canaux, USB 1.1 Full-Speed, watchdog, clocks/PLL, RNG, SHA-256, OTP, HSTX, multicœur et synchronisation.
- **Sans fil :** CYW43439, **Wi-Fi 2,4 GHz 802.11n**, Bluetooth 5.2 (BLE et Bluetooth Classic selon la pile). Ce n'est **pas** un périphérique MMIO natif du RP2350 : le processeur dialogue avec lui par bus gSPI.
- **LED intégrée :** **WL_GPIO0 de la CYW43439**. Elle **n'est pas** GPIO25 du RP2350, contrairement à la Pico 2 *sans* W. Pour un premier test, câbler une LED + résistance sur une broche extérieure (ex. **GP15**).
- **Radio câblée en interne :** GP23 `WL_ON` (activation), GP24 `WL_D` (données demi-duplex / IRQ), GP25 `WL_CS` (sélection), GP29 `WL_CLK` (horloge / ADC3 pour VSYS). Ces GPIO ne sont pas destinés à tes montages externes.
- **Débogage :** accès SWD (SWCLK, SWDIO, GND) au connecteur de debug, distinct du port USB utilisateur.

**Attention cartes W :** la mesure de VSYS/3 via ADC3 partage GP29 avec l'horloge radio. Lire VSYS pendant une phase inactive de la radio, en respectant l'état de sélection requis par le schéma. De plus, le signal de données radio partage la ligne d'interruption sur GP24. Il faut arbitrer ces accès.

**Sur le connecteur.** La carte expose 26 GPIO multifonctions, trois entrées analogiques GP26 à GP28 et 16 sorties PWM distinctes accessibles par multiplexage. La fréquence annoncée pour le processeur est de 150 MHz au maximum. Vérifier la fréquence réelle avant de calculer des diviseurs. Sur la version W, l'antenne et le CYW43439 apportent du Wi-Fi 2,4 GHz et du Bluetooth. Les variantes sans W utilisent leur LED intégrée GP25 et n'ont pas de liaison CYW43439. Le connecteur USB et le connecteur SWD ont des rôles distincts. [Fiche Pico 2 W](https://pip-assets.raspberrypi.com/categories/1088-raspberry-pi-pico-2-w/documents/RP-008304-DS-2-pico-2-w-datasheet.pdf)

## 2. Carte mémoire : ce qu'il faut mémoriser

### Régions majeures

| Plage / base | Signification | Usage dans ton OS |
|---|---|---|
| `0x00000000` | ROM de démarrage | Boot ROM et API ROM |
| `0x10000000` | Flash externe mappée XIP | Firmware persistant, code/constantes après configuration QMI/XIP |
| `0x20000000` | SRAM | Code RAM, données, piles, heap |
| `0x20000000..0x2007FFFF` | 512 KiB de SRAM en zones entrelacées | Région RAM principale ; idéale pour les premiers ELF |
| `0x20080000..0x20081FFF` | 8 KiB SRAM8/9 additionnels | Piles, structures distinctes, usages spécifiques |
| `0x40000000` | Périphériques APB | Registres de contrôle |
| `0x50000000` | Périphériques AHB | DMA, USB, PIO... |
| `0xD0000000` | SIO | GPIO rapide, FIFO, spinlocks, etc. |
| `0xE0000000` | Registres privés Cortex-M33 | NVIC, SCB, SysTick, debug CPU |

**Piège classique.** ne pas confondre la **plage générale** `0x40000000` et l'adresse d'un périphérique particulier. Les adresses non décodées peuvent provoquer un *bus fault*. Les miroirs XIP et les espaces Secure/Non-secure ne sont pas interchangeables sans comprendre le mode d'exécution.

### Adresses de base utiles du RP2350

Les valeurs suivantes sont des **bases MMIO**, pas nécessairement des registres utilisables à l'offset zéro :

| Bloc | Base | Ce que tu y trouves |
|---|---:|---|
| `CLOCKS` | `0x40010000` | Sources/diviseurs des horloges |
| `RESETS` | `0x40020000` | Reset et RESET_DONE des périphériques |
| `IO_BANK0` | `0x40028000` | Mux GPIO, statuts, interruptions |
| `PADS_BANK0` | `0x40038000` | Isolation, pull-up/down, drive, input enable |
| `XOSC` | `0x40048000` | Oscillateur cristal |
| `PLL_SYS` / `PLL_USB` | `0x40050000` / `0x40058000` | PLL |
| `ACCESSCTRL` | `0x40060000` | Contrôle d'accès / sécurité |
| `UART0` / `UART1` | `0x40070000` / `0x40078000` | UART |
| `SPI0` / `SPI1` | `0x40080000` / `0x40088000` | SPI matériels |
| `I2C0` / `I2C1` | `0x40090000` / `0x40098000` | I²C |
| `ADC` / `PWM` | `0x400A0000` / `0x400A8000` | Mesures analogiques / signaux périodiques |
| `TIMER0` / `TIMER1` | `0x400B0000` / `0x400B8000` | Compteurs 64 bits + alarmes |
| `HSTX_CTRL` | `0x400C0000` | Sérialisation parallèle sur plusieurs broches |
| `WATCHDOG` | `0x400D8000` | Watchdog/redémarrage |
| `TRNG` / `SHA256` | `0x400F0000` / `0x400F8000` | Aléatoire matériel / hachage |
| `POWMAN` / `TICKS` | `0x40100000` / `0x40108000` | Énergie / générations de ticks |
| `DMA` | `0x50000000` | DMA |
| `USBCTRL_DPRAM` | `0x50100000` | RAM dédiée USB |
| `USBCTRL_REGS` | `0x50110000` | Contrôleur USB |
| `PIO0` / `PIO1` / `PIO2` | `0x50200000` / `0x50300000` / `0x50400000` | Machines d'état PIO |
| `SIO` | `0xD0000000` | Single-cycle IO |
| `PPB` | `0xE0000000` | Bloc privé Cortex-M33 (NVIC/SCB...) |

**Différence majeure RP2040 → RP2350 :** les bases, le découpage d'IRQ, les offsets de certains registres et les fonctionnalités de sécurité changent. Ne jamais importer un `rp2040_regs.h` sans audit.

## 3. Registres, bits et volatile : la grammaire du matériel

Un octet contient 8 bits. En hexadécimal, deux chiffres représentent un octet. `0x20` vaut 32 et désigne le bit 5 lorsqu'il sert de masque. `1u << 5` déplace un 1 de cinq positions vers la gauche. Un registre de 32 bits contient quatre octets. Sur cette cible, l'octet de poids faible occupe l'adresse la plus basse (*little-endian*). Dans les exemples, un Kio vaut 1 024 octets et un Mio vaut 1 024 Kio.

En C, `uintptr_t` est un type entier capable de stocker une adresse. Le cast en `volatile uint32_t *` dit au compilateur que cette adresse désigne un accès matériel de 32 bits. Le `*` extérieur lit ou écrit à cette adresse suivant l'usage. Une affectation `REG32(adresse) = valeur` se traduit par une écriture, généralement `STR` en Thumb. Une lecture se traduit généralement par `LDR`. Employer la largeur et l'alignement exigés par le bloc.

```c
#include <stdint.h>
#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define BIT(n)         (1u << (n))

// Exemple d'accès direct : lecture du numéro de cœur
#define SIO_BASE_EXEMPLE 0xD0000000u
static inline uint32_t numero_coeur(void)
{
    return REG32(SIO_BASE_EXEMPLE + 0x000u); // 0 sur le premier cœur.
}
```

- **RO** : lecture seule ; **WO** : écriture seule ; **RW** : lecture/écriture.
- **W1C** : *write 1 to clear* ; on efface un événement en écrivant **1** sur son bit, pas en écrivant 0.
- **SET/CLR/XOR** : certains registres possèdent des variantes atomiques dédiées ; c'est préférable au `reg |= mask` si deux cœurs ou IRQ utilisent le même registre.
- **Alias APB atomiques RP2350** (si documentés pour le registre) : base `+0x1000` XOR, `+0x2000` SET, `+0x3000` CLR. **Ne pas appliquer aveuglément** aux registres avec sémantique spéciale ni à toute région AHB/SIO.
- `volatile` empêche certaines optimisations d'accès mémoire, **mais n'est ni un verrou de concurrence ni une barrière mémoire universelle**. Les périphériques asynchrones, DMA et caches nécessitent parfois `DMB`/`DSB`/`ISB` et une stratégie de cohérence.
- Les champs réservés doivent rester à leur valeur prescrite. Éviter les écritures pleine largeur improvisées si le registre contient plusieurs champs.

### Utiliser l'en-tête RP2350 sans SDK

Dans l'IDE, choisir **Projet → Nouveau projet…**, la carte **Pico 2** ou **Pico 2 W**, puis l'environnement **Bare metal · sans Pico SDK**. Le fichier arrive dans `include/`, que le CMake du projet ajoute au chemin d'inclusion. Pour un projet existant, copier le même en-tête dans son dossier d'inclusion. Il dépend uniquement de `stdint.h`. [En-tête commenté](../examples/pico2-bare-metal/include/rp2350_regs.h)

Les noms des registres donnent des **adresses**. `RP2350_RO32` les lit et une affectation à `RP2350_REG32` y écrit. `RP2350_BIT`, `RP2350_FIELD_GET` et `RP2350_FIELD_PREP` manipulent des valeurs. Les alias `RP2350_ALIAS_SET/CLR/XOR` calculent une autre adresse. Les employer sur un registre qui les documente, comme `RESETS.RESET`. SIO fournit ses propres registres SET/CLR/XOR. Pour acquitter un statut W1C, écrire son masque directement au registre.

```c
#include "rp2350_regs.h"

static inline uint32_t revision_du_rp2350(void)
{
    const uint32_t identifiant = RP2350_RO32(RP2350_SYSINFO_CHIP_ID);
    return RP2350_SYSINFO_CHIP_REV(identifiant);
}

/* À appeler après la configuration de GP15 comme sortie SIO. */
static inline void led_externe_allumer(void)
{
    RP2350_REG32(RP2350_SIO_GPIO_OUT_SET) = RP2350_BIT(15u);
}
```

Les deux cartes ont la même carte mémoire RP2350A. `PICO2_LED_GPIO` désigne la GPIO25 de la **Pico 2**. `PICO2_W_LED_CYW43_GPIO` désigne une sortie du composant radio sur la **Pico 2 W**. Utiliser le pilote radio pour cette LED. Une LED externe sur GP15 suit le même code pour les deux profils. Vérifier l'horloge, la sortie de reset et le mux avant l'accès au périphérique.

## 4. Ton premier programme : LED externe sur GP15, en RAM

**Matériel :** GP15 → résistance 330 Ω → anode LED ; cathode → GND. Les GPIO sont en logique **3,3 V**, jamais brancher une entrée GPIO directement au 5 V.

### Registres réellement nécessaires

| Adresse / formule | Sens |
|---|---|
| `IO_BANK0 + 0x004 + 8*n` | `GPIO[n]_CTRL`, champ `FUNCSEL` bits `[4:0]` ; `5` = SIO |
| `PADS_BANK0 + 0x004 + 4*n` | Pad : `ISO` bit 8 (isolation), `OD` bit 7 (désactivation sortie), `IE` bit 6, `PUE` bit 3, `PDE` bit 2 |
| `SIO + 0x018` | `GPIO_OUT_SET`, bits GPIO0..31 |
| `SIO + 0x020` | `GPIO_OUT_CLR`, bits GPIO0..31 |
| `SIO + 0x038` | `GPIO_OE_SET`, activation de la sortie |
| `SIO + 0x004` | `GPIO_IN`, état des GPIO0..31 |

Le fragment ci-dessous résume le trajet de GP15. Il s'exécute dans une fonction, après la sortie de reset. Le projet complet contrôle aussi le retour de `RESET_DONE`.

```c
#define IO_BANK0_BASE   0x40028000u
#define PADS_BANK0_BASE 0x40038000u
#define SIO_BASE        0xD0000000u
#define LED_GP          15u
#define LED_BIT         BIT(LED_GP)

/* À appeler quand les horloges, resets et permissions sont prêts. */
static inline void gpio15_exemple(void)
{
    REG32(SIO_BASE + 0x040u) = LED_BIT;          // Désactiver la sortie.
    REG32(SIO_BASE + 0x020u) = LED_BIT;          // Préparer le niveau bas.
    REG32(IO_BANK0_BASE + 4u + 8u*LED_GP) = 5u;  // Choisir SIO.
    REG32(PADS_BANK0_BASE + 0x3000u + 4u + 4u*LED_GP) =
        BIT(8) | BIT(7) | BIT(3) | BIT(2);       // CLR de ISO, OD et des pulls.
    REG32(SIO_BASE + 0x038u) = LED_BIT;          // output enable
    REG32(SIO_BASE + 0x018u) = LED_BIT;          // HIGH
    REG32(SIO_BASE + 0x020u) = LED_BIT;          // LOW
}
```

**Ne pas interpréter cette recette comme une garantie « juste après reset brut » :** après un reset global, il faut vérifier que `IO_BANK0`/`PADS_BANK0` ne sont plus en reset, que l'accès n'est pas bloqué et que la configuration des pads est cohérente. Si le débogueur arrive après le boot ROM ou un programme déjà lancé, il est possible que ces conditions soient déjà établies ; ne pas dépendre silencieusement de cet état.

### Exemple prêt à compiler

Depuis la racine de PicoDebugger, compiler le projet [pico2-bare-metal](../examples/pico2-bare-metal/Makefile). `main.c` contient l'application et le démarrage C. `sram.ld` décrit le placement. L'ELF conserve les lignes C et les deux témoins `temoin_data` et `etape`.

```sh
make -C examples/pico2-bare-metal
arm-none-eabi-readelf -h -l examples/pico2-bare-metal/build/blink_sram.elf
arm-none-eabi-objdump -d -S examples/pico2-bare-metal/build/blink_sram.elf
```

1. Hors tension, repérer **GP15, broche physique 20**, et **GND**, par exemple broche 18. Relier GP15 à l'anode de la LED via la résistance. La cathode va à GND. Le numéro GP désigne un signal du microcontrôleur. Le numéro de broche désigne sa position sur le connecteur. [Brochage officiel des Pico W](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html#wireless-board-layout)
2. Brancher la Debug Probe au PC et sa sortie **D** au connecteur SWD de la carte. Alimenter la carte par son USB. Dans PicoDebugger, sélectionner la variante de Pico 2, passer en **Carte réelle · SWD** et connecter la sonde.
3. Ouvrir `blink_sram.elf`. En mode matériel, cette action charge les symboles pour le débogueur. Sélectionner ensuite **Cible → Téléverser l'ELF sur la carte… → RAM** pour écrire les segments `PT_LOAD` en SRAM et préparer l'entrée Cortex-M33. Une coupure efface la SRAM. Après un reset, téléverser de nouveau pour relancer cet ELF en SRAM. [Téléversement ELF en RAM](HARDWARE_DEBUG.md)
4. À l'arrêt initial, examiner les deux premiers mots de `0x20000000`. Le premier donne l'adresse initiale de pile, ici `0x20040000`. Le second désigne `Reset_Handler` et porte le bit Thumb. GDB affiche généralement le PC sans ce bit. Poser un point d'arrêt à `main`, puis continuer.
5. À `main`, `temoin_data == 0x2350` et `etape == 0`. Continuer pour voir la LED clignoter, ou avancer jusqu'à une écriture de `etape`. `1` correspond à la sortie haute, `2` à la sortie basse et `0xff` à l'expiration de l'attente de `RESET_DONE`. Le délai visuel est une boucle approximative. Le [timer matériel](#8-timer-système--lindispensable-pour-le-scheduler) donne ensuite une durée mesurable.

**Lire l'exemple dans l'ordre.** Le fichier ELF indique les adresses de chargement et le point d'entrée. Le script de lien place les vecteurs à la base de la SRAM et réserve de la place pour la pile. Le démarrage installe `VTOR`, initialise `.data` et `.bss`, puis appelle `main`. L'application sort les deux banques GPIO du reset, prépare la broche et change son niveau par SIO. Le débogueur peut montrer un registre valide alors que la LED reste éteinte si le mux, le pad, la résistance ou l'orientation de la LED est incorrect.

| Symptôme | Première vérification |
|---|---|
| Le chargement réussit, le PC part à une adresse inattendue | Les vecteurs, `MSP`, `MSPLIM`, le mode Arm et l'état de sécurité de la cible |
| `etape == 0xff` | Bits 6 et 9 de `RESETS.RESET_DONE` à `0x40020008` |
| `etape` alterne, la LED reste éteinte | `IO_BANK0.GPIO15_CTRL` à `0x4002807c`, pad à `0x40038040`, GP15 physique et sens de la LED |
| Le clignotement est trop rapide | Mettre un point d'arrêt ou passer au timer pour mesurer la durée |
| L'émulateur s'arrête sur un accès MMIO ou une instruction | Pour ce starter Cortex-M33, utiliser la carte réelle. Le modèle n'implémente qu'une partie du RP2350. L'exemple C autonome de l'IDE sert à commencer en simulation. |

Le démarrage du premier exercice masque les IRQ configurables via `cpsid i`. Pour les travaux avec interruptions, remplacer l'entrée du vecteur concerné par le gestionnaire, préparer les autorisations et exécuter `cpsie i` après l'initialisation. La SRAM, la Flash et le mode BOOTSEL sont expliqués au chapitre suivant.

## 5. Comprendre le boot : deux scénarios distincts

### A. Démarrage autonome depuis la Flash persistante

```text
Alimentation ou reset
  → Boot ROM interne
      → BOOTSEL demandé : téléchargement USB
      → sinon : recherche des métadonnées IMAGE_DEF dans la Flash
          → validation de l'image et sélection d'Arm/RISC-V
              → vecteurs et démarrage du firmware
                  → copie de .data, remise à zéro de .bss, puis main()
```

**Différence avec le RP2040.** La ROM du RP2350 utilise le format **PICOBIN** et un bloc **IMAGE_DEF** pour identifier le type d'image. Le bloc de notre exemple suit immédiatement les vecteurs, dans les 4 premiers Kio. Le script de lien vérifie la position. Un ancien `boot2` de RP2040 ou la conversion brute d'un ELF lié en SRAM ne construit pas cette disposition. Une puce configurée pour un démarrage authentifié impose en plus sa propre politique. [Documentation de la ROM](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf) · [Champs PICOBIN](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/common/boot_picobin_headers/include/boot/picobin.h)

**Compiler le même clignotement en Flash.** La cible `flash` ajoute [image_def.c](../examples/pico2-bare-metal/image_def.c) et sélectionne [flash.ld](../examples/pico2-bare-metal/flash.ld). La VMA (*Virtual Memory Address*) est l'adresse utilisée par le code. La LMA (*Load Memory Address*) indique d'où viennent les octets initiaux. Ici, le code et la table des vecteurs sont dans la fenêtre Flash XIP. Les variables de `.data` ont une VMA SRAM et une LMA Flash. Le startup copie leurs octets au démarrage, puis initialise `.bss` en SRAM.

```sh
make -C examples/pico2-bare-metal flash
arm-none-eabi-readelf -l examples/pico2-bare-metal/build/blink_flash.elf
arm-none-eabi-objdump -s -j .image_def examples/pico2-bare-metal/build/blink_flash.elf
```

Sur une carte de test dont la Flash peut être remplacée, installer **picotool** sur le PC. Il prépare le fichier de chargement et ne devient pas une dépendance du firmware.

```sh
make -C examples/pico2-bare-metal uf2
picotool info -a examples/pico2-bare-metal/build/blink_flash.uf2
```

Confirmer que le fichier indique **RP2350 ARM Secure** et que l'image vise la Flash. Brancher la carte en maintenant **BOOTSEL**, puis copier `build/blink_flash.uf2` de ce projet sur le volume `RP2350`. La carte redémarre et l'ELF sert encore pour les symboles du débogueur. Retirer la sonde pour confirmer un démarrage autonome. Si la LED reste éteinte, vérifier qu'un breakpoint en `Reset_Handler` est atteint après reset, puis contrôler `temoin_data` et `etape`. En cas d'erreur de chargement BOOTSEL, relever le diagnostic UF2 avec picotool et consulter les errata de la révision de puce.

La production de l'ELF et le placement du bloc IMAGE_DEF ont été vérifiés. La conversion UF2 et le démarrage Flash restent à valider sur cette configuration. [Commandes picotool et familles UF2](https://github.com/raspberrypi/picotool#uf2)

### B. Chargement direct en RAM via SWD

```text
Débogueur stop core -> upload segments ELF en SRAM -> fixer MSP / PC,
VTOR si nécessaire -> break main -> run / step -> inspecter MMIO
```

Ici, **aucun `IMAGE_DEF` Flash n'est requis pour exécuter le code déjà chargé par le débogueur**. Cela ne contourne pas les contraintes du processeur (configuration ARM, sécurité, MSP/VTOR, état du périphérique et des clocks). C'est la voie la plus courte pour tes essais de pas à pas.

## 6. Horloges, resets et énergie : la partie cachée derrière `gpio_init()`

Il y a plusieurs horloges avec des fonctions différentes : **ROSC** (oscillateur de démarrage), **XOSC** (référence quartz), **PLL_SYS**, **PLL_USB**, puis `clk_ref`, `clk_sys`, `clk_peri`, `clk_usb`, etc.

Checklist générique pour activer un bloc :

1. Lire la configuration courante `CLOCKS` avant d'en changer les sources. Sur un firmware lancé par le boot ROM, la situation n'est pas nécessairement celle d'un reset matériel brut.
2. Déterminer quelle **clock** alimente le périphérique. UART/SPI requièrent notamment `clk_peri`.
3. Ôter le bloc du reset via `RESETS.RESET` ; attendre le bit correspondant dans `RESETS.RESET_DONE` avec un **timeout**.
4. Configurer mux GPIO et pads ; choisir les diviseurs, les registres du périphérique, puis l'activer.
5. En cas de blocage `RESET_DONE`, vérifier d'abord la source de clock : attendre sans fin ne la fait pas apparaître.
6. Modifier une PLL ou `clk_sys` seulement en conservant une source sûre ; recalculer UART/timers/SPI après les changements.

Pour un OS : écrire un composant `clock.c`, un `reset.c`, et consigner les fréquences réelles connues. Ne pas enfouir des nombres magiques « 150 MHz » ou « 12 MHz » dans chaque driver.

## 7. Interruptions Cortex-M33 : le trajet complet

Une interruption est une demande asynchrone d'un périphérique. Le contrôleur de source la **lève**, le **NVIC** du cœur décide si elle peut préempter, le processeur empile automatiquement le contexte minimal et saute vers la fonction dont l'adresse figure dans la **table des vecteurs**. Au retour d'exception, le contexte est restauré.

### Les trois portes à ouvrir

1. **Périphérique :** autoriser l'événement dans son `INTE`, effacer un ancien événement via `INTR` si W1C.
2. **NVIC :** autoriser la ligne IRQ du cœur (`NVIC_ISER0` à `0xE000E100` pour IRQ 0–31, puis ISER1 pour IRQ supérieures).
3. **Processeur :** ne pas masquer globalement les exceptions (ex. `PRIMASK`), et avoir des vecteurs/une pile valides.

Après l'événement : **acquitter la source dans le périphérique**, et non seulement une indication NVIC. Sinon l'interruption se déclenche immédiatement à nouveau.

Le RP2350 possède **52 numéros IRQ externes par cœur, dont des lignes réservées**. Par exemple les quatre alarmes `TIMER0` utilisent **IRQ 0 à 3** et `TIMER1` **IRQ 4 à 7**. Pour une IRQ externe numéro `n`, son vecteur Cortex-M est à l'indice **`16 + n`** dans la table. Le RP2350 comporte également les dimensions Secure/Non-secure : le contrôleur d'accès, la cible de l'IRQ et la table de vecteurs doivent correspondre au domaine exécuté.

### Vecteurs et données du processeur

- `SCB.VTOR` : `0xE000ED08` (version Secure/Non-secure suivant le contexte).
- `NVIC.ISER0` : `0xE000E100` ; `NVIC.ICER0` : `0xE000E180` ; `NVIC.ICPR0` : `0xE000E280`.
- Entrée 0 vecteurs : valeur initiale de **MSP** ; entrée 1 : **Reset_Handler** ; entrées 2..15 : exceptions système ; entrées 16+ : IRQ des périphériques.
- Respecter l'alignement VTOR, l'adresse Thumb des handlers, les priorités, et la configuration du domaine Secure.

Pour un petit OS : commencer avec **une seule IRQ TIMER0** ; ensuite introduire un vecteur dispatché, puis des handlers courts qui ne font que acquitter, enregistrer un événement et réveiller une tâche.

## 8. Timer système : l'indispensable pour le scheduler

Le RP2350 n'a pas exactement le même chemin de timer que le RP2040. **TIMER0** (`0x400B0000`) et **TIMER1** (`0x400B8000`) ont chacun un compteur 64 bits et quatre alarmes 32 bits. Le bloc **TICKS** (`0x40108000`) crée les impulsions temporelles à partir de `clk_ref`.

### Registres TIMERn importants

| Offset | Registre | Rôle |
|---|---|---|
| `+0x0C` | `TIMELR` | Lecture partie basse du compteur ; verrouille la partie haute |
| `+0x08` | `TIMEHR` | Lecture partie haute après `TIMELR` |
| `+0x10` à `+0x1C` | `ALARM0..3` | Prochaine échéance sur les 32 bits bas |
| `+0x20` | `ARMED` | Etat des alarmes |
| `+0x28` | `TIMERAWL` | Lecture immédiate partie basse |
| `+0x3C` | `INTR` | Statut brut / acquittement W1C |
| `+0x40` | `INTE` | Autorisation locale IRQ |
| `+0x44` | `INTF` | Forçage IRQ (tests) |
| `+0x48` | `INTS` | Statut masqué IRQ |

### Registres TICKS pour TIMER0

- `TICKS + 0x18` : `TIMER0_CTRL`, bit 0 `ENABLE`.
- `TICKS + 0x1C` : `TIMER0_CYCLES` : nombre de cycles de `clk_ref` par tick.
- `TICKS + 0x20` : `TIMER0_COUNT`, compteur interne du diviseur.

**Exemple :** avec `clk_ref = 12 MHz` *réellement confirmé*, arrêter le générateur, écrire `CYCLES = 12`, puis `CTRL.ENABLE = 1` donne un tick d'**1 µs**. Ne pas mettre 12 si la référence est différente. Faire de même pour TIMER1 si utilisé.

### Recette de la première alarme

Le registre `TIMER0.SOURCE` à `+0x38` choisit le tick déclenché par TICKS avec 0, ou `clk_sys` avec 1. `LOCKED` à `+0x34` protège la configuration. Partir d'un TIMER0 hors reset (bit 23), non verrouillé et réservé à l'exercice. Convenir de la fréquence de référence avant de toucher au diviseur. Voici la version à 12 MHz. Le premier test observe le compteur et l'alarme sans IRQ. Il évite d'avoir à diagnostiquer en même temps le timer et le NVIC.

```c
#define TIMER0_BASE 0x400b0000u
#define TICKS_BASE  0x40108000u

static inline int timer0_configurer_tick_1us(void)
{
    REG32(TICKS_BASE + 0x18u) = 0u; // Désactive TIMER0_CTRL.ENABLE.
    for (unsigned i = 0; i < 100000u; ++i) {
        if (REG32(TICKS_BASE + 0x18u) & BIT(1)) continue; // RUNNING.
        REG32(TICKS_BASE + 0x1cu) = 12u; // 12 cycles de clk_ref par tick.
        REG32(TIMER0_BASE + 0x38u) = 0u;  // SOURCE = tick.
        REG32(TICKS_BASE + 0x18u) = 1u;    // Active le générateur.
        for (unsigned j = 0; j < 100000u; ++j)
            if (REG32(TICKS_BASE + 0x18u) & BIT(1)) return 1;
        return 0;
    }
    return 0;
}

static inline uint64_t timer0_lire(void)
{
    /* TIMELR fige le mot haut. Un seul lecteur utilise cette paire ici. */
    const uint32_t bas = REG32(TIMER0_BASE + 0x0cu);
    const uint32_t haut = REG32(TIMER0_BASE + 0x08u);
    return ((uint64_t)haut << 32) | bas;
}

static inline void timer0_armer_10ms(void)
{
    REG32(TIMER0_BASE + 0x20u) = BIT(0); // ARMED, annule une ancienne alarme.
    REG32(TIMER0_BASE + 0x3cu) = BIT(0); // INTR, efface l'ancien événement.
    REG32(TIMER0_BASE + 0x10u) =
        REG32(TIMER0_BASE + 0x28u) + 10000u; // Écrit ALARM0 en dernier.
}
```

Dans une boucle de test, vérifier que deux lectures du temps progressent. Programmer ensuite l'alarme. Au bout de 10 ms, `INTR & 1` doit devenir non nul, même si `INTE=0`. Une lecture répétée de `TIMERAWL` et une soustraction non signée mesurent des durées courtes au passage du zéro. Pour un échantillon 64 bits simultané depuis plusieurs contextes, protéger la lecture mémorisée ou lire `TIMERAWH`, `TIMERAWL`, puis `TIMERAWH` et recommencer si la moitié haute a changé.

**Puis ajouter l'IRQ.** Dans le projet de démarrage, placer un gestionnaire pour l'IRQ externe 0 à l'indice 16 de la table. Effacer le drapeau dans le timer. Effacer un éventuel pending NVIC via `ICPR0` à `0xe000e280`. Autoriser le bit 0 dans `TIMER0.INTE` à `+0x40` et dans `NVIC.ISER0` à `0xe000e100`. Armer l'alarme, puis démasquer les IRQ avec `cpsie i` dans le domaine prévu. L'appel au gestionnaire peut être observé par un compteur en SRAM.

```c
volatile uint32_t alarmes_observees;

void timer0_irq0(void)
{
    REG32(TIMER0_BASE + 0x3cu) = BIT(0); // Acquitte la source en W1C.
    ++alarmes_observees;                  // État lisible dans le débogueur.
    REG32(TIMER0_BASE + 0x10u) =
        REG32(TIMER0_BASE + 0x28u) + 10000u; // Réarme depuis l'instant courant.
}
```

Cette récurrence courte dérive avec la latence du gestionnaire. Une périodicité plus stable conserve sa prochaine date avec `prochaine += periode` et saute les échéances déjà passées. L'alarme compare une **égalité** sur les 32 bits bas. Si l'instant programmé est passé, le déclenchement peut attendre le tour suivant. À 1 MHz, un tour dure environ 71 min 35 s. Un arrêt de débogage peut aussi figer le timer si `DBGPAUSE` le prévoit. [TIMER RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/timer.h) · [TICKS RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/ticks.h)

Pour un OS, comparer d'abord **SysTick** (simple, par cœur), **TIMER0** (base temps partagée, 64 bits) et **AON timer** (réveils basse consommation). Le choix d'un scheduler tickless favorisera les alarmes programmables plutôt qu'un tick périodique constant.

## 9. Catalogue des périphériques : la recette pour chacun

Les lignes ci-dessous donnent **le protocole d'initialisation**, les familles de registres à rechercher et l'usage typique. Elles ne remplacent pas l'inventaire exhaustif de centaines de champs de la datasheet : celle-ci reste la référence normative pour **chaque masque/offset**.

| Périphérique | Algorithme d'emploi sans SDK | Point de vigilance |
|---|---|---|
| **GPIO/SIO** | Débloquer resets → régler pad (ISO, IE, PUE/PDE, OD) → `GPIOx_CTRL.FUNCSEL=5` → `SIO.GPIO_OE` → `GPIO_OUT/IN` | Ne pas confondre mux et direction ; GPIO internes radio réservés |
| **IRQ GPIO** | Choisir front/niveau dans `IO_BANK0`, activer le masque sur le bon cœur et domaine Secure → NVIC → acquitter le front | Débounce logiciel si bouton mécanique, risques E9 sur entrées haute impédance |
| **UART0/1** | Sélectionner clock `clk_peri`, reset, mux `GPIO_FUNC_UART`, programmer diviseurs baud + `LCR_H`, `CR`, FIFO/IRQ | Calcul du baud selon clock réelle, TX/RX croisés, console avant scheduler |
| **SPI0/1** | Horloge/reset → mux SPI sur broches compatibles → `CPSR`, `CR0`, `CR1` → `DR`/statut `SR` → IRQ/DMA si besoin | Modes CPOL/CPHA, chip-select, demi/full duplex ; radio W utilise un bus gSPI spécifique et PIO |
| **I²C0/1** | PADS avec pull-up adaptés → mux I²C → horloge/reset → `IC_CON`, `IC_TAR`, `IC_SS_SCL_*`, `IC_ENABLE`, FIFO `IC_DATA_CMD` | Résistances pull-up physiques, erreurs/ACK, clock stretching |
| **PWM** | Mux PWM → choisir slice + channel → `DIV`, `TOP`, `CC`, `CSR.EN` | Fréquence = clock/((diviseur)*(TOP+1)), conflit GPIO/slices |
| **ADC** | Configurer pad entrée analogique → horloge/reset ADC → sélectionner entrée, activer ADC → surveiller `CS.READY`/`RESULT`, FIFO/DMA optionnels | Entrées GP26-28 ; GP29 sert aussi à VSYS/radio, respecter plage analogique |
| **DMA** | Lire/écrire adresses src/dst, count, taille/incréments, `TREQ` DREQ périphérique, puis `CTRL_TRIG.EN` | Alignement, ownership des buffers, barrières, IRQ de fin et concurrence multicœur |
| **PIO0/1/2** | Assembler instructions PIO, copier dans RAM d'instructions PIO → config pin mapping/shift/diviseur → state machine → FIFO/IRQ/DMA | Ressources locales limitées ; excellent pour gSPI CYW43439 et protocoles atypiques |
| **USB** | Horloge USB 48 MHz et reset → USB DPRAM et controller → endpoints / descriptors → activation USB et IRQ | C'est un **contrôleur USB** : une pile USB minimale reste à écrire pour CDC, HID, etc. |
| **HSTX** | Configurer clock, modes/voies, FIFO/DMA → sorties et timing | Débit élevé, configurations pin et signal soigneuses |
| **WATCHDOG** | Définir timeout/tick → activer → nourrir périodiquement → tester reboot | Bien distinguer panne/timeout voulu ; écriture erronée peut redémarrer sans diagnostic |
| **TRNG** | Init/clock du bloc → contrôle de disponibilité → lecture des mots | Vérifier statut/erreurs avant utilisation sécurité |
| **SHA-256** | Configurer mode → alimenter en mots → lire digest après `READY` | Format/endian et DMA/buffers |
| **Multicœur / SIO** | Démarrer core1 via protocole de lancement défini → FIFO/doorbells/spinlocks → synchroniser | Piles séparées, cohérence mémoire, IRQ partagées, core1 au repos au départ |
| **POWMAN/AON** | Choisir horloges basse consommation → préparer réveil → sommeil/dormant → restauration | Les périphériques/clocks ne survivent pas tous aux mêmes états |
| **Flash/QMI/XIP** | Contrôleur QMI, commandes Flash, désactivation/gestion XIP pour écrire, cache/invalidation | Ne pas effacer la Flash depuis du code qui continue à s'exécuter en XIP de façon non maîtrisée |
| **BOOTROM/OTP** | Lire spécifications API ROM et config irréversible OTP avant toute programmation | OTP irréversible ; ne jamais l'utiliser comme registre d'expérimentation |

**Ordre pédagogique conseillé :** GPIO → UART → TIMER/IRQ → PWM → I²C → SPI → DMA → PIO → USB → multicœur → Flash/bootloader → radio. À chaque chapitre : carte des registres, trace MMIO, exemple autonome, diagnostic des erreurs.

## 10. CYW43439 : contacter la radio sans boîte noire

### 10.1 Architecture matérielle

```text
RP2350 (Cortex-M33)
   ├ GP23 : WL_ON (reset / alimentation logique radio)
   ├ GP25 : WL_CS (chip select)
   ├ GP29 : WL_CLK (horloge gSPI)
   └ GP24 : WL_D (data bidirectionnelle + IRQ hors transaction)
         │
       bus gSPI, souvent implémenté avec PIO sur la Pico 2 W
         │
      CYW43439 : bus interne « backplane », RAM, cœurs radio
         ├ firmware Wi-Fi + calibration / paramètres CLM
         ├ contrôle Wi-Fi : SDPCM, ioctl/iovar, événements
         └ Bluetooth : transport HCI spécifique CYW43
```

**Ce n'est pas un SPI 4 fils MOSI/MISO classique** : la ligne données est demi-duplex ; direction, temporisations, bits de commande et phases de réponse suivent le protocole gSPI de la puce. Le PIO est utile pour générer des échanges fiables, mais un pilote de bring-up peut explorer un mode de transfert plus lent si le timing est respecté.

### 10.2 De zéro à « lire un registre radio » : étapes concrètes

1. **Réserver GP23/24/25/29** ; désactiver toute utilisation concurrente (notamment lecture ADC VSYS sur GP29).
2. **Init GPIO/PIO** : GP23 sortie, GP25 sélection sortie, GP29 clock sortie ; GP24 données bidirectionnelles et mode entrée pour l'IRQ en dehors des transferts. Régler les pads et la direction selon la phase.
3. **Reset de la puce** : WL_ON bas, temporisation, WL_ON haut, temporisation. Reprendre les délais du port de référence de la carte et les mesurer à l'analyseur logique.
4. **Init gSPI** : mode/ordre des bits, commande 32 bits gSPI, transfert de mots/payload, délais de turnaround, bus function 0/1/2, vérification d'un registre d'identification connu.
5. **Accès au backplane** : les adresses internes longues sont accessibles en sélectionnant une *fenêtre* d'adresse (registres high/mid/low), puis en lisant/écrivant la portion basse. C'est une notion différente du MMIO RP2350.
6. **Téléchargement de firmware radio** : utiliser une image compatible CYW43439 et le bloc de paramètres CLM/réglementation du pays ; transférer en RAM interne de la radio, initialiser les paramètres/NVRAM et libérer son cœur selon la séquence du pilote.
7. **Démarrer les canaux de communication** : réception/émission de paquets, flow control, crédits, interruption/sondage et timeouts.
8. **Wi-Fi** : implémenter les messages **SDPCM**, demandes de contrôle **ioctl/iovar**, événements de scan/association, échange de trames réseau ; ajouter ensuite une pile **Ethernet/IP/TCP/UDP/DHCP/DNS** (ex. lwIP, ou pile maison).
9. **Bluetooth** : activer la partie radio Bluetooth, initialiser son firmware selon le transport, puis échanger des **paquets HCI** sur le bus partagé ; ajouter une pile hôte (ex. BTstack ou maison) pour GAP/GATT/L2CAP, etc.
10. **LED intégrée** : `WL_GPIO0` est une GPIO **du contrôleur radio** : après initialisation de la puce, sa commande transite par le pilote CYW43, pas par `SIO.GPIO25`.

**Ne pas confondre trois niveaux de commande :**

- **gSPI** = lire/écrire des registres ou blocs de données dans la puce ; protocole de transport.
- **SDPCM + iovars** = commandes au firmware Wi-Fi ; gestion des scans, connexion et paquets.
- **HCI** = interface standard *hôte ↔ contrôleur Bluetooth*, emballée dans un transport spécifique CYW43 (le pilote Raspberry Pi ajoute un petit en-tête de **4 octets** à ses paquets HCI).

Les détails d'opcodes gSPI, des formats SDPCM/HCI et les timings exacts doivent être repris **du code pilote cyw43-driver versionné** et testés au logic analyzer. Les firmwares CYW43439 ne sont pas à recréer ; leur provenance, licence et région de fonctionnement restent à respecter.

### 10.3 Le chemin de lecture à étudier pour écrire ton propre pilote

Dans le dépôt de référence [georgerobotics/cyw43-driver](https://github.com/georgerobotics/cyw43-driver) :

- `src/cyw43_spi.c` / `src/cyw43_spi.h` : format bas niveau gSPI, fonctions de transfert.
- `src/cyw43_ll.c` : accès aux registres et *backplane window*, téléchargement firmware, protocole SDPCM, contrôle Wi-Fi.
- `src/cyw43_ctrl.c` : séquences d'initialisation/arrêt et points d'entrée de contrôle, transport Bluetooth.
- [pico-sdk : `btstack_hci_transport_cyw43.c`](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_cyw43_driver/btstack_hci_transport_cyw43.c) : encapsulation des paquets HCI dans le transport CYW43.
- [Documentation NuttX Pico 2 W](https://nuttx.incubator.apache.org/docs/latest/platforms/arm/rp23xx/boards/raspberrypi-pico-2-w/index.html) : contre-exemple intéressant d'intégration OS avec réseau, et de PIO gSPI.

**Mini-exemple côté transport.** Cette interface permet de tester une lecture sans dépendre d'une pile réseau. Implémenter le rappel gSPI à partir du port PIO de référence. `registre_test` et `attendu` viennent de la documentation de la puce ou de la révision du pilote. Le rappel renvoie 1 uniquement quand il a reçu un mot valide avant son délai.

```c
typedef int (*lecture_gspi_t)(uint8_t fonction, uint32_t adresse, uint32_t *mot);

static inline int verifier_transport_cyw43(lecture_gspi_t lire,
                                            uint32_t registre_test, uint32_t attendu)
{
    uint32_t recu = 0;
    return lire(0u, registre_test, &recu) == 1 && recu == attendu;
}
```

Observer CS, CLK et WL_D. Confirmer qu'après la phase de commande, le RP2350 libère WL_D avant la réponse et configure à nouveau la broche pour l'IRQ en fin de transaction. [Port gSPI PIO de la Pico W](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c)

**Progression du pilote.** ne pas écrire la pile TCP/IP au moment où tu testes gSPI. D'abord afficher une lecture d'identifiant radio correcte en UART ; puis charger le firmware compatible et allumer la LED radio ; puis faire un scan Wi-Fi ; puis échanger un paquet. Chaque étape doit pouvoir être déboguée isolément.

## 11. Le squelette d'un mini-OS sur Pico 2 W

Un découpage cohérent en C :

```text
boot/       startup.S, vectors.S, sram.ld, flash.ld, image_def.S
soc/rp2350/ mmio.h, addressmap.h, clocks.c, reset.c, gpio.c,
            irq.c, timer.c, dma.c, pio.c, flash.c
board/pico2w/ board.h, pinmux.c, cyw43_bus_pio.c, board_led.c
kernel/     irq_dispatch.c, scheduler.c, tasks.c, heap.c, syscalls.c
net/        cyw43_transport.c, cyw43_fw.c, wifi_control.c, bt_hci.c
app/        main.c, tests/
tools/      elf_inspect.py, uf2_pack.py, register_dump.py
```

Deux choix d'architecture OS à clarifier plus tard :

- **Petit noyau privilégié unique** : toutes les tâches partagent le même espace physique. Très simple sur MCU, mais pas d'isolation mémoire générale par MMU. Le **MPU Cortex-M33** peut aider à isoler des régions / permissions ; ce n'est pas une MMU de Linux.
- **RTOS préemptif** : timer → IRQ → PendSV → sauvegarde/restauration des registres de tâche, piles distinctes, ordonnanceur, priorités. Commencer mono-cœur, puis seulement ajouter core1.

**Débogage dès le départ** : journaliser `PC`, `LR`, `xPSR`, `MSP`, `PSP`, `CFSR`, `HFSR`, `BFAR`, `MMFAR`, `VTOR`, statuts périphériques. En cas de HardFault, analyser l'**adresse fautive**, le domaine Secure/Non-secure, le SP et la faute bus avant de toucher au code applicatif.

## 12. Progression en 10 exercices vérifiables

1. ELF SRAM minimal, breakpoint en `main` ; lire CPUID SIO.
2. Faire clignoter une **LED externe GP15**, en observant SIO et PADS.
3. Réaliser une sortie **UART0** sans bibliothèque (`putchar` en polling) ; tracer l'état du débogueur.
4. Compter le temps avec `TICKS + TIMER0` ; vérifier fréquence au logic analyzer.
5. Déclencher la **première interruption TIMER0/IRQ0**, acquitter et reprogrammer.
6. Ajouter un **scheduler coopératif**, puis `PendSV` pour préemption simple.
7. Lire un capteur **I²C** ; ensuite utiliser DMA ou PIO pour un transfert.
8. Fabriquer un firmware **Flash bootable RP2350** avec `IMAGE_DEF` ; tester hors SWD.
9. Écrire le **transport gSPI radio minimal**, lire un registre CYW43439, pour valider le transport.
10. Charger le firmware CYW43439, utiliser `WL_GPIO0`, faire un **scan Wi-Fi**, puis Bluetooth HCI, puis protocoles réseau.

À chaque exercice, exiger une **preuve** : valeur de registre avant/après, trace UART, capture logique ou test observable. Tu peux faire produire ces étapes par Codex sans le laisser introduire de dépendance au Pico SDK.

## 13. Sources primaires et références de code

1. [**RP2350 datasheet officielle**](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf) — cartographie, cœurs, SIO, horloges, interruptions, ROM et registres. La pagination varie avec la révision.
2. [**Pico 2 W datasheet officielle**](https://pip-assets.raspberrypi.com/categories/1088-raspberry-pi-pico-2-w/documents/RP-008304-DS-2-pico-2-w-datasheet.pdf) — matériel, pinout, CYW43439, régulateur, GPIO internes.
3. [**Documentation Raspberry Pi, gamme Pico**](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html) — capacité, pinout W, radio, sécurité, licences.
4. [**En-têtes de registres RP2350, Pico SDK**](https://github.com/raspberrypi/pico-sdk/tree/2.2.0/src/rp2350/hardware_regs/include/hardware/regs) — `addressmap.h`, `sio.h`, `io_bank0.h`, `pads_bank0.h`, `timer.h`, `ticks.h`, `resets.h` ; **référence de définitions**, sans obligation de linker le SDK.
5. [**ROM RP2350, code source**](https://github.com/raspberrypi/pico-bootrom-rp2350) — référence pour le processus de boot.
6. [**Pilote CYW43**](https://github.com/georgerobotics/cyw43-driver) — couches SPI, firmware radio, contrôle Wi-Fi/Bluetooth.
7. [**BTstack / interface HCI CYW43**](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_cyw43_driver/btstack_hci_transport_cyw43.c) — encapsulation transport Bluetooth.
8. [**Guide Cortex-M33 Arm**](https://developer.arm.com/documentation/100235/latest/) — registres CPU, exceptions, NVIC, SysTick, TrustZone.
9. [**NuttX sur Pico 2 W**](https://nuttx.incubator.apache.org/docs/latest/platforms/arm/rp23xx/boards/raspberrypi-pico-2-w/index.html) — lecture d'une implémentation OS libre sur cette carte.

### Ce qui reste volontairement hors du premier guide

Une table **exhaustive** de tous les champs de tous les registres RP2350 ferait des centaines de pages, serait redondante avec la datasheet et évoluerait avec les révisions du silicium. Pour un projet de système, l'option maintenable consiste à **générer** des headers dérivés des définitions matérielles du fabricant, puis à écrire ici les recettes, contraintes et séquences de programmation que la simple table de registres ne raconte pas. Le décodage exhaustif des paquets gSPI/SDPCM et le firmware Flash autonome doivent être validés dans des chapitres/artefacts distincts avant de les considérer comme « testés sur cible ».


---

# Complément pédagogique — du bus aux pilotes bare metal

## 14. Pourquoi initialiser un périphérique demande plusieurs opérations

Un microcontrôleur est un *système sur puce* (SoC). Les périphériques ne sont pas des fonctions C : ce sont des circuits matériels autonomes reliés au CPU par une interconnexion. Le CPU émet des transactions de lecture/écriture vers des adresses physiques ; le contrôleur concerné reçoit ces transactions et modifie son état interne.

**Analogie informatique :** l'adresse MMIO joue le rôle d'une interface de contrôle, les registres sont des variables d'état matérielles, les IRQ sont des événements asynchrones, le DMA est un moteur de transfert indépendant du CPU, et le mux GPIO est un commutateur qui relie une fonction interne à une broche physique.

| Couche | Pourquoi existe-t-elle ? | Ce que le programme doit vérifier | Quand peut-on l'ignorer ? |
|---|---|---|---|
| Alimentation / domaines | Réduire la consommation, séparer des tensions et alimenter des composants externes | Domaine alimenté ; puce externe activée | Souvent déjà réglé pour les périphériques internes au démarrage |
| Oscillateur / PLL | Produire des références temporelles précises | Source, stabilité, fréquence | Si le chargeur a déjà configuré les horloges et que cette hypothèse est vérifiée |
| Clock gate / diviseur | Cadencer un bloc sans faire tourner tous les autres | Horloge réellement disponible et fréquence connue | Pour certains blocs dont l'horloge est permanente |
| Reset | Mettre un circuit dans un état déterministe ; éviter qu'il travaille pendant l'initialisation | Bit reset levé et statut `RESET_DONE` | Pour un bloc déjà sorti de reset |
| Contrôle d'accès | Isoler les ressources entre domaines Secure/Non-secure et maîtres du bus | Permissions CPU/DMA, état de sécurité | Si les permissions héritées sont connues et suffisantes |
| GPIO mux | Plusieurs fonctions se partagent une même broche | `FUNCSEL` correspondant à la fonction voulue | Pour les périphériques sans broches externes (timer, DMA…) |
| Pads | Adapter les caractéristiques électriques | Isolation, entrée activée, pulls, drive, sortie désactivée ou non | Sans objet pour les périphériques purement internes |
| Registres du bloc | Définir la fonction réelle : vitesse, mode, seuils, FIFO | Paramètres et statut du périphérique | Jamais pour une fonction non triviale |
| IRQ / DMA | Éviter le polling CPU et transférer des blocs efficacement | Source d'événement, acquittement, routage NVIC ou DREQ DMA | Si polling simple suffisant |

### 14.1 Exemple raisonné : envoyer un octet en UART

1. Choisir UART0 et ses broches compatibles à l'aide du tableau de fonctions GPIO **du RP2350**.
2. Vérifier la fréquence de `clk_peri`, car les diviseurs de baudrate dépendent de cette fréquence.
3. Lever le reset UART0 dans `RESETS`, puis attendre le bit de confirmation correspondant dans `RESET_DONE`.
4. Configurer les pads et sélectionner la fonction UART des GPIO choisis.
5. Désactiver temporairement l'UART pendant la modification de ses paramètres, si la séquence prescrite l'exige.
6. Écrire `UARTIBRD` et `UARTFBRD` (diviseur entier et fractionnaire), puis `UARTLCR_H` (format, FIFO).
7. Activer TX/RX et l'UART dans `UARTCR`.
8. Attendre que la FIFO TX accepte une donnée (`UARTFR.TXFF == 0`), puis écrire l'octet dans `UARTDR`.
9. Pour recevoir, attendre `UARTFR.RXFE == 0`, puis lire `UARTDR` et examiner les éventuels bits d'erreur.

Ce n'est **pas** une recette « universelle d'initialisation » : le timer n'a pas de mux GPIO, et le PIO a son propre programme d'instructions, ses FIFOs et ses machines d'état.

### 14.2 Les règles de lecture/écriture des registres

| Convention | Signification | Erreur fréquente |
|---|---|---|
| `RO` | Lecture seule | Tenter de configurer un statut |
| `WO` | Écriture seule | Faire un `|=` : cela suppose une lecture préalable |
| `RW` | Lecture/écriture | Écraser des bits réservés ou d'autres champs |
| `W1C` | Écrire `1` pour acquitter | Faire un read-modify-write qui acquitte des événements involontairement |
| `W1S` | Écrire `1` pour positionner | Écrire `0` en croyant effacer |
| FIFO | Chaque accès peut retirer/ajouter une donnée | Lire deux fois pour « vérifier » et consommer deux éléments |
| SET/CLR/XOR | Alias ou registres dédiés pour opérations atomiques | Utiliser les alias sur un bloc non compatible |

`volatile` force les accès observables mais ne rend pas les opérations atomiques, ne verrouille pas les accès concurrents et ne remplace pas les barrières mémoire nécessaires dans certains scénarios DMA/multicœur.

## 15. Mémoire, fenêtres MMIO et exceptions de bus

### 15.1 La différence entre *région*, *fenêtre* et *registre*

- **Région** : grande portion de l'espace physique (par exemple les périphériques APB).
- **Fenêtre de périphérique** : plage que l'interconnexion route vers un contrôleur particulier.
- **Registre** : adresse/offset effectivement implémenté à l'intérieur de cette fenêtre.
- **Trou réservé** : adresse dans une fenêtre qui ne correspond pas nécessairement à un registre documenté.

Une plage inclusive de taille `N` octets, commençant à `BASE`, se termine à `BASE + N - 1`. Exemple : `0x40070000 + 0x8000 - 1 = 0x40077FFF`. **Cette borne décrit une fenêtre de 32 KiB, pas 32 KiB de registres valides.** Vérifier la taille réelle des fenêtres dans la carte d'adressage du fabricant, notamment pour les blocs AHB, SIO et PPB, qui n'ont pas nécessairement la même granularité.

| Fenêtre APB RP2350 | Début | Fin calculée pour 32 KiB | Usage |
|---|---|---|---|
| CLOCKS | `0x40010000` | `0x40017FFF` | Sources/diviseurs d'horloge |
| RESETS | `0x40020000` | `0x40027FFF` | Mise en/hors reset |
| IO_BANK0 | `0x40028000` | `0x4002FFFF` | Mux GPIO et IRQ |
| PADS_BANK0 | `0x40038000` | `0x4003FFFF` | Caractéristiques électriques |
| ACCESSCTRL | `0x40060000` | `0x40067FFF` | Permissions d'accès |
| UART0 | `0x40070000` | `0x40077FFF` | Série asynchrone |
| UART1 | `0x40078000` | `0x4007FFFF` | Deuxième UART |
| SPI0 | `0x40080000` | `0x40087FFF` | SPI matériel |
| SPI1 | `0x40088000` | `0x4008FFFF` | Deuxième SPI |
| I2C0 | `0x40090000` | `0x40097FFF` | I²C matériel |
| I2C1 | `0x40098000` | `0x4009FFFF` | Deuxième I²C |
| ADC | `0x400A0000` | `0x400A7FFF` | Conversion analogique |
| PWM | `0x400A8000` | `0x400AFFFF` | Génération de PWM |
| TIMER0 | `0x400B0000` | `0x400B7FFF` | Temps/4 alarmes |
| TIMER1 | `0x400B8000` | `0x400BFFFF` | Second timer |

**Attention :** les bornes ci-dessus sont celles des emplacements APB à pas de `0x8000`, utiles pour se repérer. Elles ne prouvent ni que chaque octet est décodé ni que l'accès est autorisé. Les régions SRAM, ROM, XIP, AHB, SIO et PPB doivent être traitées séparément, sans extrapoler cette règle.

### 15.2 Qu'est-ce qu'un BusFault ?

Une instruction `LDR` ou `STR` demande un accès à une adresse. L'interconnexion doit déterminer quel composant répond et si la transaction est acceptée. Une adresse non mappée, un accès interdit ou un périphérique incapable de répondre correctement peuvent entraîner une faute. Une adresse **au-delà d'une fenêtre** peut simplement appartenir au périphérique suivant ; inversement une adresse **dans** une fenêtre peut être réservée. Certains accès invalides peuvent renvoyer une valeur ou être ignorés : ne jamais s'appuyer sur ce comportement sans documentation.

Sur Cortex-M33, inspecter `SCB.CFSR` (cause), `SCB.HFSR`, `SCB.BFAR` si l'adresse fautive est valide, et la pile d'exception. Si BusFault n'est pas activée comme exception configurable, elle peut escalader en HardFault. En debug, distinguer aussi faute de sécurité, faute mémoire, défaut d'alignement et exécution d'une adresse invalide.

```c
#define MMIO32(a) (*(volatile uint32_t *)(uintptr_t)(a))
// Ne jamais « scanner » aveuglément toutes les adresses d'un espace MMIO :
// certaines lectures ont des effets de bord et certaines écritures sont irréversibles.
```

## 16. PIO : un coprocesseur spécialisé pour les signaux GPIO

Le RP2350 possède **3 blocs PIO × 4 machines d'état = 12 machines d'état**. Un programme PIO est une petite séquence d'instructions dédiée aux transferts de bits et au contrôle temporel, **pas du code ARM**. Les machines disposent de registres de configuration, d'un compteur d'instructions, de registres de décalage et de FIFOs TX/RX. La mémoire d'instructions est partagée entre les machines d'un même bloc PIO.

| Composant PIO | Fonction | Utilisation |
|---|---|---|
| `SM` (*state machine*) | Exécute le programme PIO | Une SM par flux/protocole indépendant |
| `IN`, `OUT` | Décalent des bits entre GPIO et registres internes | Réception/émission série |
| `SET`, `MOV` | Modifient broches et registres de SM | Contrôle des niveaux et états |
| `WAIT`, `JMP` | Attendent un événement ou branchent | Synchronisation, boucles |
| `PUSH`, `PULL` | Échangent des mots avec les FIFO | Interface CPU ↔ PIO |
| `IRQ` | Synchronisent ou signalent un événement | Notification / coopération entre SM |
| `CLKDIV` | Divise l'horloge de la machine | Fixe le rythme d'exécution |
| `PINCTRL`, `EXECCTRL`, `SHIFTCTRL` | Définissent broches, wrap, décalage, FIFO | Paramétrage de la SM |
| `TXF`, `RXF`, `FSTAT` | Données et états de FIFO | Échange avec CPU/DMA |

### 16.1 Exemple conceptuel : émettre des bits

```pio
.program serial_tx
.wrap_target
    pull block        ; attendre un mot TX venant du CPU ou du DMA
    set x, 7          ; envoyer 8 bits
bitloop:
    out pins, 1       ; placer un bit sur la broche
    jmp x-- bitloop   ; répéter huit fois
.wrap
```

Cet exemple illustre le modèle, **pas un protocole UART/SPI complet** : il manque les délais calibrés, le cadrage, l'horloge ou les bits start/stop selon le protocole. Les paramètres `OUT pins`, la direction des broches, l'autopull et le diviseur doivent être configurés depuis le CPU.

### 16.2 Recette pour faire fonctionner une machine PIO

1. Choisir `PIO0`, `PIO1` ou `PIO2`, puis une machine libre et ses GPIO.
2. Vérifier horloge/reset/permissions du bloc et configurer pads + mux des GPIO sur la fonction PIO correspondante.
3. Charger les instructions PIO dans la mémoire d'instructions du bloc ; conserver l'offset de chargement.
4. Configurer la SM : broches d'entrée/sortie, sens des broches, registres de décalage, seuils de FIFO, diviseur d'horloge, wrap, adresse de départ.
5. Initialiser l'état des FIFO et les valeurs de sortie ; démarrer la SM.
6. Alimenter la FIFO TX, ou lire la FIFO RX ; pour de gros volumes, brancher les requêtes DREQ sur un canal DMA.
7. Vérifier `FSTAT`, les interruptions PIO et les niveaux GPIO au débogueur ou à l'analyseur logique.

**PIO contre SPI matériel :** le SPI matériel n'est pas intrinsèquement plus lent. Il est souvent plus simple et très efficace pour du SPI standard, y compris avec DMA. Le PIO est préférable quand il faut changer le sens d'une ligne à un cycle précis, produire un protocole non standard, ou combiner plusieurs signaux selon un timing spécifique. Le débit dépend de l'horloge, du nombre d'instructions par bit, des contraintes électriques et du protocole ; il n'existe pas de gain automatique du PIO.

## 17. Wi-Fi / Bluetooth : les couches réelles de la Pico 2 W

```text
Application (socket, scan, BLE)
    ↓
Pile réseau TCP/IP ou pile Bluetooth hôte
    ↓
Commandes/paquets du contrôleur CYW43439 (Wi-Fi : SDPCM et commandes associées ; BT : HCI)
    ↓
Pilote de transport gSPI + gestion des IRQ / buffers
    ↓
PIO du RP2350 + GPIO23/24/25/29
    ↓
CYW43439 exécutant son propre firmware
    ↓
Radio 2,4 GHz / antenne
```

**Pourquoi du SPI peut être rapide :** un lien série synchrone peut fonctionner à plusieurs dizaines de MHz ; le débit utile dépend ensuite des en-têtes, de la direction de la ligne, des temps morts, du coût des IRQ et des copies mémoire. Par exemple, à 50 MHz, une ligne à un bit/cycle offre *théoriquement* 50 Mbit/s bruts, **sans préjuger de la fréquence effectivement supportée ni du débit réel de la Pico 2 W**.

**Particularité du gSPI câblé sur la Pico 2 W :** l'interface est adaptée au câblage radio : une ligne de données bidirectionnelle (`WL_D`) et des séquences de retournement de direction, avec partage du signal d'interruption. Ce n'est pas un périphérique SPI 4 fils ordinaire, dans lequel MOSI et MISO sont deux lignes séparées. Le PIO permet de réaliser précisément ces séquences. Les formats de commandes, bits d'adresse, ordre des bits, cycles d'attente, registres de la CYW43439 et procédure de chargement du firmware doivent être implémentés **d'après le pilote de référence et la documentation du contrôleur**, pas devinés à partir de SPI générique.

### 17.1 Étapes d'un pilote radio bare metal

1. Initialiser GPIO/PIO et les lignes `WL_ON`, `WL_CS`, `WL_D`, `WL_CLK` ; vérifier les niveaux électriques et l'état de reset.
2. Mettre en service le contrôleur radio avec les temporisations exigées.
3. Implémenter les primitives gSPI lecture/écriture, y compris changement de direction, délais et gestion des interruptions.
4. Lire un identifiant ou un registre documenté pour valider le transport **avant** toute pile réseau.
5. Charger et démarrer le firmware radio compatible, ainsi que ses paramètres nécessaires ; vérifier la réponse du contrôleur.
6. Implémenter la couche de paquets/commandes Wi-Fi, puis une interface Ethernet logique pour une pile IP (par exemple lwIP, indépendante du Pico SDK).
7. Pour Bluetooth, implémenter le transport HCI adapté au contrôleur et intégrer une pile hôte Bluetooth ; distinguer transport HCI et profils/services Bluetooth.

La radio est donc **un deuxième ordinateur spécialisé**, avec son firmware et ses protocoles. Un OS RP2350 peut posséder son propre pilote gSPI et sa pile réseau sans réécrire le firmware radio.

## 18. Fiches périphériques : ce que c'est, comment le démarrer, où lire les registres

Les **bases** ci-dessous sont celles du RP2350 ; pour les bornes des fenêtres APB, voir §15.1. Pour les blocs AHB/PPB, ne pas inférer une fin de plage à partir de la base suivante. Les registres cités sont les **points d'entrée utiles**, pas une transcription exhaustive de tous les bits. Les noms et offsets exacts sont à contrôler dans les headers de registres RP2350 du fabricant avant toute écriture.

| Bloc | À quoi il sert | Base | Dépendances et première opération | Registres/champs à étudier |
|---|---|---|---|---|
| `RESETS` | Réinitialiser les contrôleurs | `0x40020000` | Écrire les bits de reset, attendre confirmation | `RESET`, `RESET_DONE` |
| `CLOCKS` | Distribuer les fréquences | `0x40010000` | Choisir source, vérifier stabilité | `CLK_*_CTRL`, `CLK_*_DIV`, `CLK_*_SELECTED` |
| `XOSC`, `PLL_SYS`, `PLL_USB` | Produire des horloges stables/rapides | `0x40048000`, `0x40050000`, `0x40058000` | Démarrage oscillateur, verrouillage PLL avant sélection | `CTRL`, `STATUS`, diviseurs PLL |
| `IO_BANK0` | Affecter fonctions aux GPIO | `0x40028000` | Définir `FUNCSEL` pour chaque broche | `GPIOx_STATUS`, `GPIOx_CTRL`, registres IRQ |
| `PADS_BANK0` | Régler l'interface électrique | `0x40038000` | Vérifier isolation et direction | `GPIOx`, `ISO`, `OD`, `IE`, `PUE`, `PDE`, `DRIVE` |
| `SIO` | GPIO rapides, synchronisation et fonctions CPU | `0xD0000000` | Sélectionner SIO dans le mux | `GPIO_IN`, `GPIO_OUT`, `GPIO_OE`, `*_SET/CLR/XOR` |
| `UART0/1` | Série asynchrone | `0x40070000` / `0x40078000` | Horloge, reset, mux, baudrate | `UARTDR`, `UARTFR`, `UARTIBRD`, `UARTFBRD`, `UARTLCR_H`, `UARTCR` |
| `SPI0/1` | Série synchrone standard | `0x40080000` / `0x40088000` | Horloge, reset, mux, format, CS | `SSPCR0`, `SSPCR1`, `SSPDR`, `SSPSR`, `SSPCPSR` |
| `I2C0/1` | Bus 2 fils adressé | `0x40090000` / `0x40098000` | Horloge, reset, GPIO open-drain/pulls, vitesse | `IC_CON`, `IC_TAR`, `IC_DATA_CMD`, `IC_STATUS`, `IC_ENABLE` |
| `PWM` | Rapport cyclique programmable | `0x400A8000` | Horloge, mux GPIO PWM, tranche | `CHx_CSR`, `CHx_DIV`, `CHx_CTR`, `CHx_CC`, `CHx_TOP` |
| `ADC` | Mesurer des tensions | `0x400A0000` | Activation ADC, choix canal et pad analogique | `CS`, `RESULT`, `FCS`, `FIFO`, `DIV` |
| `TIMER0/1` | Temps et alarmes matérielles | `0x400B0000` / `0x400B8000` | Vérifier source de tick, programmer alarme | `TIME*`, `ALARM0..3`, `INTR`, `INTE`, `INTS` |
| `DMA` | Transférer sans boucle CPU | `0x50000000` | Canal, adresses, taille, DREQ, démarrage | `READ_ADDR`, `WRITE_ADDR`, `TRANS_COUNT`, `CTRL_TRIG` |
| `PIO0/1/2` | Protocole GPIO programmable | `0x50200000`, `0x50300000`, `0x50400000` | Horloge/reset, mux, programme, SM | `CTRL`, `FSTAT`, `TXF`, `RXF`, `INSTR_MEM`, `SMx_*` |
| `USBCTRL` | USB device/host | `0x50110000` | Clock USB, reset, PHY, buffers dédiés | `MAIN_CTRL`, `SIE_CTRL`, `SIE_STATUS`, registres endpoints |
| `WATCHDOG` | Redémarrer si le programme se bloque | `0x400D8000` | Configurer timeout et alimentation du compteur | `CTRL`, `LOAD`, `REASON`, `SCRATCH*` |
| `TRNG` | Aléa matériel | `0x400F0000` | Vérifier statut et conditions d'usage | registres de contrôle/statut et sortie aléatoire |
| `SHA256` | Accélération du hachage | `0x400F8000` | Configurer, injecter données, attendre résultat | contrôle, données, état, digest |
| `ACCESSCTRL` | Permissions par maître et domaine | `0x40060000` | Vérifier état Secure/Non-secure | registres d'autorisation de chaque cible |
| `POWMAN` | Gestion d'énergie | `0x40100000` | Vérifier mode et sources d'alimentation | contrôle d'état et réveil |
| `HSTX` | Sortie numérique haut débit | `0x400C0000` | Horloge, mux, configuration de sortie | contrôle HSTX et FIFO |
| `NVIC / SCB / SysTick` | IRQ, exceptions et timer CPU | espace `0xE0000000` (PPB) | Table vectorielle et configuration Cortex-M33 | `VTOR`, `ISER`, `ICER`, `IPR`, `CFSR`, `SYST_*` |

### 18.1 Lire une fiche et préparer les préconditions

Dans les fragments, `REG32(adresse)` et `BIT(n)` reprennent les définitions du chapitre 3. Les fonctions s'ajoutent à un projet freestanding. Tester chaque fiche sur un firmware séparé pour éviter les conflits de broches, de resets ou de canaux. Les boucles bornées ci-dessous comptent des **essais**, pas des microsecondes. Inspecter les statuts en cas d'échec avant d'augmenter la limite.

Pour traduire une description en C, relever dans l'ordre la **base**, l'**offset**, les **bits**, le **type d'accès** et la **valeur après reset**. Par exemple, `UART0 + UARTFR = 0x40070000 + 0x18 = 0x40070018`. Le bit `TXFF` à 5 donne le masque `1u << 5`, soit `0x20`. Lire le registre, appliquer le masque, puis tester si le résultat est nul. Cette lecture interroge un circuit matériel. Elle ne lit pas une variable que le programme a initialisée.

```c
/* À appeler après avoir démarré l'horloge qui alimente le bloc. */
static inline int sortir_du_reset(uint32_t masque)
{
    REG32(0x40023000u) = masque; // RESETS.RESET, alias CLR.
    for (unsigned i = 0; i < 100000u; ++i)
        if ((REG32(0x40020008u) & masque) == masque) return 1;
    return 0; // Le statut RESET_DONE ne confirme pas la sortie du reset.
}
```

| Bit de `RESETS.RESET` | Blocs des exercices |
|---:|---|
| 0, 2, 3, 4, 5 | ADC, DMA, HSTX, I²C0, I²C1 |
| 6, 9 | IO_BANK0 et PADS_BANK0 |
| 11 / 12 / 13 | PIO0 / PIO1 / PIO2 |
| 14, 15, 16, 17 | PLL_SYS, PLL_USB, PWM, SHA256 |
| 18, 19, 23, 24, 25 | SPI0, SPI1, TIMER0, TIMER1, TRNG |
| 26, 27, 28 | UART0, UART1, USBCTRL |

Ce tableau indique les numéros de bits, pas des adresses. Les contrôleurs XIP et QMI demandent une procédure distincte. Éviter de les réinitialiser au cours d'une exécution en Flash. [Cartographie RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/addressmap.h) · [Champs RESETS](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/resets.h)

### 18.2 GPIO en entrée et interruptions de broche

Un bouton entre **GP14 et GND** peut utiliser une résistance de tirage vers le haut (**pull-up**). La broche lit 1 au repos et 0 lorsque le bouton ferme le circuit. Pour GP14, configurer `FUNCSEL = 5` dans `IO_BANK0 + 4 + 8 × 14`. Désactiver la sortie via `SIO.GPIO_OE_CLR`. Dans le pad, régler `IE=1`, `PUE=1`, `PDE=0`, puis retirer `ISO`. Activer une résistance externe si les exigences électriques ou la révision A2 le demandent. Un bouton mécanique fait souvent plusieurs transitions rapides. Détecter un état stable sur quelques millisecondes avec le timer avant d'enregistrer un appui.

```c
static inline int bouton_appuye(void)
{
    return (REG32(0xd0000004u) & BIT(14)) == 0u; // SIO.GPIO_IN.
}
```

Pour une IRQ externe, chaque GPIO occupe quatre bits dans les registres bruts `IO_BANK0.INTR[n]` à partir de `+0x230`. Le groupe `n` vaut `gpio/8`. Dans le groupe de quatre bits, les positions 0 à 3 représentent niveau bas, niveau haut, front descendant et front montant. Pour GP14, le front descendant est le bit `(14 % 8) × 4 + 2 = 26` dans `INTR1`, à `0x40028234`. Effacer ce front par écriture directe de `BIT(26)`, puis autoriser le même bit dans le registre `INTE1` du cœur et du domaine choisis. Relier ensuite la sortie de banque au NVIC. L'IRQ Secure du banc 0 est 21. Un niveau actif persiste tant que la broche reste dans cet état. Un front doit être acquitté. [Registres IO_BANK0](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/io_bank0.h) · [Table IRQ](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/intctrl.h) · [Erratum GPIO E9 et révision A4](https://www.raspberrypi.com/news/rp2350-a4-rp2354-and-a-new-hacking-challenge/)

### 18.3 Horloges, UART et premières traces

**Horloges.** Le quartz sur la carte sert de référence XOSC à 12 MHz. Confirmer l'activation et la stabilité de XOSC avant de le sélectionner. Une PLL produit une fréquence plus élevée en se verrouillant sur la référence. `CLOCKS.CLK_REF_CTRL` à `0x40010030` et `CLK_REF_SELECTED` à `+0x38` permettent de vérifier la source active. `CLK_SYS_CTRL/DIV/SELECTED` à `+0x3c/0x40/0x44` indiquent le chemin système. Une source sélectionnée et son diviseur donnent une fréquence théorique. Le bloc `FC0` la mesure par rapport à une référence connue. Utiliser un oscilloscope ou un analyseur logique quand la fréquence ou le signal de référence n'est pas confirmé.

**Démarrer le quartz.** Ce premier pas suppose que le cœur tourne déjà sur une source valide. Sur Pico 2/2 W, le quartz est de 12 MHz. Le compteur de stabilisation s'exprime en groupes de 256 périodes dans le réglage utilisé ici. La valeur 47 correspond à environ 1 ms. Une fois le bit `STABLE` visible, la source est utilisable. Ce fragment démarre XOSC et ne change pas l'horloge système.

```c
static inline int demarrer_xosc_12mhz(void)
{
    const uintptr_t xosc = 0x40048000u;
    if (REG32(xosc + 4u) & BIT(31)) return 1;  // Déjà stable après le boot.
    REG32(xosc + 0x0cu) = 47u;               // STARTUP.
    REG32(xosc) = (0xfabu << 12) | 0xaa0u;   // ENABLE, gamme 1 à 15 MHz.
    for (unsigned i = 0; i < 1000000u; ++i)
        if (REG32(xosc + 4u) & BIT(31)) return 1;
    return 0;
}
```

Pour un premier firmware qui maîtrise toute sa référence, sélectionner ensuite XOSC dans `CLK_REF_CTRL.SRC` avec la valeur 2, régler `CLK_REF_DIV.INT=1` et vérifier `CLK_REF_SELECTED` bit 2. Prévoir ce changement avant d'activer les pilotes dépendants. Sur un firmware pris en charge en cours d'exécution, vérifier l'arbre actuel et le garder tant que les dépendances demeurent actives. [Registres XOSC](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/xosc.h) · [Initialisation XOSC de référence](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/hardware_xosc/xosc.c)

Dans une expérience isolée, on peut alimenter **clk_peri à 12 MHz** depuis XOSC même si `clk_sys` est différent. Préconditions pour ce fragment. XOSC est stable, les UART et SPI qui utilisent `clk_peri` sont inactifs, le cœur contrôle ce partage. Attendre l'arrêt avant de changer `AUXSRC`.

```c
static inline int clk_peri_depuis_xosc(void)
{
    const uintptr_t ctrl = 0x40010048u; // CLK_PERI_CTRL.
    REG32(ctrl) = 0u;                   // Retire ENABLE et KILL.
    for (unsigned i = 0; i < 100000u; ++i) {
        if (REG32(ctrl) & BIT(28)) continue; // Attend ENABLED à zéro.
        REG32(ctrl + 4u) = 1u << 16;         // Diviseur entier égal à 1.
        REG32(ctrl) = (4u << 5) | BIT(11);  // AUXSRC XOSC et ENABLE.
        for (unsigned j = 0; j < 100000u; ++j)
            if (REG32(ctrl) & BIT(28)) return 1;
        return 0;
    }
    return 0;
}
```

Changer `clk_sys` ou une PLL demande de prévoir une source sûre le temps du changement, de vérifier `LOCK` et `SELECTED`, puis de recalculer les diviseurs des blocs dépendants. Consigner les fréquences en hertz dans les paramètres du pilote. [Registres CLOCKS](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/clocks.h)

**UART0.** Choisir GP0 pour TX, GP1 pour RX, `FUNCSEL=2` et les pads adaptés. Pour TX, retirer `ISO` et `OD`. Pour RX, activer `IE`. Connecter les masses, puis TX de la carte au RX du port U de la Debug Probe. Pour recevoir sur la carte, relier aussi TX de la sonde à RX de la carte. La série est en logique 3,3 V. Avec UART0 exclusif, le UART0 libéré du reset via le bit 26 et `clk_peri = 12 MHz`, le diviseur 64 fois agrandi vaut `arrondi(4 × 12 000 000/115 200) = 417`. Les parts entière et fractionnaire valent 6 et 33. Choisir **115200, 8N1** côté moniteur. Cela signifie 8 bits de données, aucune parité et un bit d'arrêt.

| Offset à partir de `0x40070000` | Registre | Points à vérifier |
|---:|---|---|
| `0x00` | UARTDR | Écrire pour émettre, lire pour retirer une donnée RX. Les bits 8 à 11 signalent des erreurs avec la donnée reçue. |
| `0x18` | UARTFR | `BUSY` bit 3, `RXFE` bit 4, `TXFF` bit 5 |
| `0x24 / 0x28` | UARTIBRD / UARTFBRD | Diviseurs entier et fractionnaire en soixante-quatrièmes |
| `0x2c` | UARTLCR_H | Format 8 bits dans `WLEN`, FIFO `FEN` bit 4. Écrire après les diviseurs. |
| `0x30` | UARTCR | `UARTEN` bit 0, TX bit 8, RX bit 9 |
| `0x38 / 0x40 / 0x44` | UARTIMSC / UARTMIS / UARTICR | Autorisation, statut masqué et acquittement local |

```c
/* À appeler sur UART0 fraîchement sorti du reset, sans transfert actif. */
static inline int uart0_envoyer_a(void)
{
    const uintptr_t u = 0x40070000u;
    REG32(u + 0x30u) = 0u;            // UART désactivé pendant la configuration.
    REG32(u + 0x24u) = 6u;
    REG32(u + 0x28u) = 33u;
    REG32(u + 0x2cu) = (3u << 5) | BIT(4); // 8N1 et FIFO.
    REG32(u + 0x30u) = BIT(0) | BIT(8) | BIT(9);
    for (unsigned i = 0; i < 100000u; ++i) {
        if (REG32(u + 0x18u) & BIT(5)) continue;
        REG32(u) = 'A';
        return 1; // L'octet est accepté par la FIFO.
    }
    return 0;
}
```

**Résultat.** Le moniteur unique du port U affiche `A`. Une FIFO est une file matérielle premier entré, premier sorti. Pour arrêter l'horloge ou changer de broche, attendre `BUSY=0` afin que le dernier bit sorte effectivement. Pour recevoir, lire DR une seule fois par octet après `RXFE=0`. Pour les IRQ, UART0 est la ligne 33, donc banque NVIC 1, bit 1. [Registres UART](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/uart.h) · [Moniteur série](HARDWARE_DEBUG.md#lire-les-traces-uart)

### 18.4 PWM, ADC et signaux mesurables

**PWM.** La modulation de largeur d'impulsion laisse une sortie à 1 pendant une fraction de chaque période. Le RP2350 offre 12 tranches, chacune avec deux sorties A/B partageant un compteur. La carte n'expose pas toutes les affectations possibles du silicium. Pour une première mesure, GP0 est **PWM0 A**, fonction 4. Arrêter la tranche avant de modifier le compteur. En comptage montant, fréquence `clk_sys/(DIV × (TOP+1))` et rapport cyclique `CC_A/(TOP+1)` dans l'intervalle usuel. Une tranche occupe `0x14` octets. Ajouter `0x14 × n` à `0x400a8000`.

```c
/* GP0 déjà muxé en PWM, reset PWM relâché, tranche 0 réservée. */
static inline void pwm0_quart_de_periode(void)
{
    const uintptr_t p = 0x400a8000u;
    REG32(p + 0x00u) = 0u;         // CH0_CSR, arrêt et comptage montant.
    REG32(p + 0x04u) = 8u << 4;    // CH0_DIV, entier 8 en format 8.4.
    REG32(p + 0x08u) = 0u;         // CH0_CTR.
    REG32(p + 0x10u) = 999u;       // CH0_TOP, mille valeurs par période.
    REG32(p + 0x0cu) = 250u;       // CH0_CC A à 25 %, B à zéro.
    REG32(p + 0x00u) = BIT(0);     // CH0_CSR.EN.
}
```

À 12 MHz système, on attend 1,5 kHz. À 150 MHz système, on attend 18,75 kHz. À partir de la même tranche, modifier A peut impliquer une mise à jour masquée du registre partagé avec B. `TOP` et `CC` sont mis en mémoire tampon pendant le fonctionnement. Une LED peut donner une indication de luminosité. L'analyseur logique mesure réellement les temps haut et bas. [Registres PWM](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/pwm.h)

**ADC.** Le convertisseur analogique-numérique retourne un code 12 bits entre 0 et 4095. Utiliser GP26/ADC0 pour un potentiomètre entre **ADC_VREF et AGND**, avec le curseur relié à GP26. Respecter la plage 0 à ADC_VREF et les limites électriques de la carte. La conversion dépend de la référence réelle et de la qualité du signal. En première approximation `V ≃ résultat × VREF / 4095`. Préparer `clk_adc` à 48 MHz, retirer le reset ADC bit 0 et configurer GP26 en chemin analogique. Désactiver les pulls et l'entrée numérique `IE`, bloquer la sortie `OD` et retirer l'isolation `ISO` une fois le pad préparé.

```c
/* ADC réservé à cet exercice. GP26 et clk_adc déjà configurés. */
static inline int adc0_lire(void)
{
    const uintptr_t a = 0x400a0000u;
    REG32(a) = BIT(0);                     // CS.EN et AINSEL=0.
    for (unsigned i = 0; i < 100000u; ++i) {
        if (!(REG32(a) & BIT(8))) continue; // READY.
        REG32(a) = BIT(0) | BIT(2);         // EN et START_ONCE.
        for (unsigned j = 0; j < 100000u; ++j) {
            const uint32_t cs = REG32(a);
            if (!(cs & BIT(8))) continue;
            if (cs & BIT(9)) return -1;    // ERR de la conversion.
            return (int)(REG32(a + 4u) & 0xfffu); // RESULT.
        }
        return -1;
    }
    return -1;
}
```

Les canaux d'entrée sur Pico 2 sont GP26–28 sur le connecteur. ADC3 et GP29 concernent la mesure interne de VSYS et l'horloge radio sur W. Le capteur interne correspond au canal 4 du RP2350A, après `CS.TS_EN` bit 1. Sa tension se convertit avec les caractéristiques du fabricant et une référence connue. Pour échantillonner en continu, voir `CS.START_MANY`, `FCS` à `+8`, `FIFO` à `+0x0c`, `DIV` à `+0x10`. Consommer exactement un mot par lecture FIFO. [Registres ADC](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/adc.h)

### 18.5 SPI et I²C

**SPI.** Le contrôleur envoie un bit par cycle d'une horloge fournie par le maître. Relier **GP2 SCK**, **GP3 TX** (MOSI) et **GP4 RX** (MISO) pour SPI0, fonction 1. Choisir une GPIO distincte pour la sélection **CS** et la placer à 1 avant le transfert. Masse commune et niveaux 3,3 V. Le composant externe décide du mode `CPOL/CPHA` et du débit maximal. Les deux fils de données peuvent fonctionner simultanément. Même si la réponse est ignorée, lire la FIFO RX après chaque mot pour éviter son remplissage.

| Offset sur SPI0 `0x40080000` | Rôle |
|---:|---|
| `+0x00` `SSPCR0` | Taille de mot, format, polarité/phase et diviseur SCR. |
| `+0x04` `SSPCR1` | Activation SSE bit 1, choix maître/esclave et boucle interne. |
| `+0x08` `SSPDR` | Émettre ou retirer le mot reçu. |
| `+0x0c` `SSPSR` | Place TX bit 1, données RX bit 2, activité BUSY bit 4. |
| `+0x10` `SSPCPSR` | Diviseur pair de 2 à 254. SCK vaut `clk_peri/(CPSDVSR × (1+SCR))`. |

```c
/* SPI0 hors reset, CS à gérer autour de l'appel, instance exclusive. */
static inline int spi0_echanger(uint8_t octet)
{
    const uintptr_t s = 0x40080000u;
    for (unsigned i = 0; i < 100000u; ++i) {
        if (!(REG32(s + 0x0cu) & BIT(1))) continue;
        REG32(s + 8u) = octet;
        for (unsigned j = 0; j < 100000u; ++j)
            if (REG32(s + 0x0cu) & BIT(2)) return (int)(REG32(s + 8u) & 0xffu);
        return -1;
    }
    return -1;
}
```

Pour l'initialisation en mode 0, désactiver `SSPCR1`, mettre `SSPCR0=7` (8 bits, SCR nul), `SSPCPSR=12`, puis `SSPCR1=BIT(1)`. À 12 MHz de `clk_peri`, SCK vaut 1 MHz. Pour un premier test en boucle interne, ajouter `SSPCR1.LBM` bit 0 en activant le bloc et vérifier que l'octet lu correspond. Pour un composant réel, garder CS actif jusqu'à `BUSY=0`. [Registres SPI](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/spi.h)

**I²C.** SDA porte les données, SCL l'horloge. Les appareils tirent les lignes à zéro et les résistances externes les ramènent au niveau haut. Cette sortie s'appelle un **drain ouvert**. Pour I²C0 on peut employer GP4 SDA et GP5 SCL, fonction 3. Utiliser des résistances adaptées au bus, souvent 4,7 kΩ vers 3,3 V pour un montage court. Vérifier les modules prééquipés de résistances. Le périphérique cible répond par un **ACK** (acquittement). Il peut ralentir SCL en la maintenant basse, si le mode utilisé le permet.

Configurer le reset et la fréquence de l'horloge système, désactiver `IC_ENABLE` à `+0x6c` et attendre `IC_ENABLE_STATUS` à `+0x9c` nul. Régler `IC_CON` à `+0x00` pour le mode maître, vitesse Standard et mode esclave désactivé. Écrire l'adresse **7 bits non décalée** dans `IC_TAR` à `+0x04`. Régler `IC_SS_SCL_HCNT/LCNT` à `+0x14/0x18` selon `clk_sys`, les délais imposés par le contrôleur et la capacité du bus. Réactiver `IC_ENABLE`. Mesurer la fréquence à 100 kHz avant d'essayer le mode rapide.

```c
/* Une lecture d'octet avec STOP. Préparer IC_TAR et utiliser un bus libre. */
static inline int i2c0_lire_octet(uint32_t *cause_abandon)
{
    const uintptr_t i2c = 0x40090000u;
    if (!(REG32(i2c + 0x70u) & BIT(1))) return -1; // TX FIFO pleine.
    REG32(i2c + 0x10u) = BIT(8) | BIT(9); // DATA_CMD, READ et STOP.
    for (unsigned i = 0; i < 100000u; ++i) {
        if (REG32(i2c + 0x34u) & BIT(6)) { // RAW_INTR_STAT.TX_ABRT.
            const uint32_t cause = REG32(i2c + 0x80u);
            if (cause_abandon) *cause_abandon = cause; // Retenir la cause.
            (void)REG32(i2c + 0x54u);     // CLR_TX_ABRT.
            return -2;
        }
        if (REG32(i2c + 0x70u) & BIT(3)) // RX FIFO non vide.
            return (int)(REG32(i2c + 0x10u) & 0xffu);
    }
    return -1;
}
```

Pour lire un registre de capteur, on écrit souvent le numéro du registre, puis on redémarre en lecture sans libérer le bus. C'est le **repeated START**. Le protocole du capteur dicte cette séquence et la vérification du dernier STOP avant de reconfigurer le bloc. En cas de NACK, vérifier adresse, masse, résistances et sens de transfert. En cas de timeout, lire les statuts et récupérer le bus avant de relancer. [Registres I²C](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/i2c.h)

### 18.6 DMA de la SRAM vers la SRAM

Le DMA effectue des lectures et écritures pendant que le CPU s'occupe d'autre chose. Le RP2350 a **16 canaux** et **quatre sorties IRQ**. Employer un canal par transfert actif. Pour le canal 0, la base est `0x50000000`. Par canal, ajouter `0x40 × n`. `READ_ADDR/WRITE_ADDR` sont à `+0/+4`, `TRANS_COUNT` à `+8` et `CTRL_TRIG` à `+0x0c`. `TRANS_COUNT[27:0]` compte des transferts et `[31:28]` indique le mode sur RP2350. En mode 0, quatre mots de 32 bits valent **quatre transferts**, soit 16 octets. Dans le contrôle, utiliser taille 2, incrément lecture et écriture, et `TREQ_SEL=0x3f` pour une requête permanente. Écrire CTRL_TRIG en dernier.

```c
/* Canaux et tampons restent réservés tant que le DMA travaille. */
static inline int dma0_copier_quatre_mots(const uint32_t *src, uint32_t *dst)
{
    const uintptr_t d = 0x50000000u;
    if (REG32(d + 0x0cu) & BIT(26)) return 0; // Canal déjà occupé.
    REG32(d) = (uint32_t)(uintptr_t)src;
    REG32(d + 4u) = (uint32_t)(uintptr_t)dst;
    REG32(d + 8u) = 4u;
    __asm__ volatile ("dmb sy" ::: "memory"); // Publie les écritures du CPU.
    REG32(d + 0x0cu) = BIT(0) | (2u << 2) | BIT(4) | BIT(6) | (0x3fu << 17);
    for (unsigned i = 0; i < 100000u; ++i) {
        const uint32_t status = REG32(d + 0x0cu);
        if (status & BIT(31)) return -1;     // Erreur de bus.
        if (!(status & BIT(26))) {
            __asm__ volatile ("dmb sy" ::: "memory"); // Lire dst ensuite.
            return 1;
        }
    }
    return -2; // Récupérer le canal avant de réutiliser les tampons.
}
```

Tester avec deux tableaux statiques distincts en SRAM, alignés sur 4 octets. Pour alimenter une FIFO, garder l'adresse MMIO fixe, incrémenter l'adresse en SRAM et sélectionner le **DREQ** du périphérique. DREQ signifie demande de transfert DMA. Vérifier le tableau des DREQ de la puce. Une erreur ou un timeout exige de vérifier `CTRL_TRIG`, puis d'abandonner le canal via `CHAN_ABORT` à `+0x464` selon la séquence du fabricant. Tant que le canal est occupé, préserver la validité des deux tampons. [Registres DMA du RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/dma.h)

### 18.7 PIO avec un programme de deux mots

Le chapitre 16 explique le fonctionnement d'une machine d'état. Ce fragment livre deux instructions dans **PIO0**. Il émet un carré sur GP15. Réserver le bloc, la machine SM0 et deux mots de mémoire d'instructions. La broche doit être muxée sur PIO0 (`FUNCSEL=6`). Configurer les pads une fois la sortie du bloc mise dans un état défini. Le diviseur de SM0 est composé d'un entier et de 8 bits fractionnaires.

```c
/* Réserver PIO0/SM0 à cet exemple et relâcher le bit 11 du reset. */
static inline void pio0_carre_sur_gp15(void)
{
    const uintptr_t p = 0x50200000u;
    REG32(p + 0x3000u) = BIT(0);           // Arrête SM0, alias CLR de CTRL.
    REG32(p + 0x48u) = 0xe001u;            // SET PINS, 1.
    REG32(p + 0x4cu) = 0xe000u;            // SET PINS, 0.
    REG32(p + 0xc8u) = 15000u << 16;       // SM0_CLKDIV.
    REG32(p + 0xccu) = 1u << 12;           // WRAP_TOP=1, WRAP_BOTTOM=0.
    REG32(p + 0xdcu) = (1u << 26) | (15u << 5); // SET_COUNT=1 et SET_BASE=15.
    REG32(p + 0xd8u) = 0xe081u;            // SM0_INSTR force SET PINDIRS, 1.
    REG32(p + 0xd8u) = 0x0000u;            // Force JMP 0, adresse de départ.
    REG32(p + 0x2000u) = BIT(0);           // Lance SM0, alias SET de CTRL.
}
```

À 150 MHz système, on attend 5 kHz. Le CPU peut continuer pendant que PIO produit les fronts. `FSTAT` à `+4` et `TXF0` à `+0x10` servent pour les programmes qui consomment la FIFO. Le registre d'instruction en cours `SM0_INSTR` ne relit pas le contenu de toute la RAM d'instructions. Cette RAM est partagée entre les quatre machines du bloc. Le RP2350 peut déplacer la fenêtre GPIO de certaines configurations PIO. La carte RP2350A et cet exemple utilisent GP15 dans la fenêtre par défaut. [Registres PIO](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/pio.h)

### 18.8 Faible consommation, veille et watchdog

**Watchdog.** Le chien de garde compte à rebours. Une application en fonctionnement normal recharge le compteur. S'il expire, le contrôleur redémarre les domaines sélectionnés dans `PSM.WDSEL`. Préparer le générateur watchdog dans `TICKS + 0x30/0x34`. Avec un tick de 1 µs, `WATCHDOG.LOAD` est un délai en microsecondes sur le RP2350. Garder la valeur dans 24 bits. Avant d'activer `CTRL.ENABLE` (bit 30), régler la politique de reset et de pause en débogage. Écrire LOAD pour recharger, par exemple `REG32(0x400d8004u) = 500000u;` pour 0,5 s lorsque le tick est vérifié. Charger une fois avant l'activation. Au redémarrage, lire `REASON` à `0x400d8008`, bit 0 pour expiration et bit 1 pour forçage. Les registres `SCRATCH` à partir de `+0x0c` peuvent conserver un diagnostic suivant le type de reset. Éviter ceux réservés à une convention de la ROM. [Registres WATCHDOG](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/watchdog.h) · [Domaines PSM](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/psm.h)

**Veille du cœur.** `WFI` (*Wait For Interrupt*) laisse le cœur attendre une interruption. Préparer une source capable de réveiller le cœur, par exemple l'alarme TIMER0 ci-dessus. Confirmer que cette source continue à tourner. Une IRQ peut se produire avant l'instruction d'attente. Pour une boucle de tâches, la décision « aucun travail » et l'entrée en veille doivent donc être synchronisées.

```c
static inline void attendre_evenement_irq(void)
{
    __asm__ volatile ("dsb sy\n\twfi" ::: "memory"); // Attendre avec la source de réveil prête.
}
```

**POWMAN et minuterie toujours alimentée.** AON signifie *Always-On*, un domaine prévu pour rester alimenté dans les modes compatibles. `POWMAN` commence à `0x40100000`. Examiner `STATE` à `+0x38` pour les domaines et `TIMER` à `+0x88` pour l'horloge/activation de la minuterie. Lire les moitiés hautes et basses à `+0x70/0x74`. Essai en lecture seule. Lire haut, bas, haut et recommencer si le haut a changé. Pour une mesure en veille, configurer le timer AON depuis une source compatible avec le mode choisi, régler l'alarme et vérifier le réveil avant de couper d'autres horloges. Certaines écritures POWMAN exigent un mot de passe et des transitions documentées. Entrer en `dormant` suppose aussi de rétablir les horloges des périphériques au réveil. [Registres POWMAN](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/powman.h)

### 18.9 Multicœur, synchronisation et cœur ARM

**Lancement.** Chaque Cortex-M33 a sa pile, son état NVIC et sa table de vecteurs. Les SRAM et périphériques sont partagés. Au reset, le cœur 1 suit le protocole de démarrage de la ROM. Sur une configuration ARM compatible, le dialogue FIFO comporte une synchronisation puis la table de vecteurs, le sommet de pile et l'adresse de code du second cœur. Suivre l'écho de chaque mot dans la version de ROM utilisée. Choisir une pile disjointe et protéger tout périphérique partagé. Employer l'échange FIFO simple ci-dessous après que le cœur 1 a quitté la ROM. [ROM RP2350](https://github.com/raspberrypi/pico-bootrom-rp2350) · [Séquence multicœur de référence](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_multicore/multicore.c)

```c
static inline int fifo_lire_message(uint32_t *message)
{
    if (!(REG32(0xd0000050u) & BIT(0))) return 0; // FIFO_ST.VLD.
    *message = REG32(0xd0000058u);                 // FIFO_RD retire un mot.
    __asm__ volatile ("dmb sy" ::: "memory");      // Rend le tampon publié lisible ensuite.
    return 1;
}
```

Côté producteur, vérifier `FIFO_ST.RDY` bit 1, publier les données en SRAM, poser une barrière puis écrire `FIFO_WR` à `0xd0000054`. La FIFO transmet ici une notification ou un index de tampon. Pour une notification simple, les **doorbells** de SIO partent de `+0x180`. Un **spinlock** est un verrou acquis en essayant en boucle. Les 32 adresses commencent à `SIO+0x100` avec un pas de 4. Une lecture peut **acquérir** le verrou. Éviter que le rafraîchissement automatique du débogueur le lise. Réserver les numéros et protéger la section critique par les barrières appropriées. Sur certains steppings, consulter les errata SIO avant d'utiliser des spinlocks matériels. Les interpolateurs SIO commencent autour de `+0x80`. Ils servent aux calculs répétitifs de masquage, décalage et addition. Observer une valeur connue dans `INTERP0_ACCUM0` avant de lire une sortie PEEK/POP, qui peut mettre à jour l'accumulateur selon le mode. [Registres SIO](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/sio.h)

**Cortex-M33.** `SysTick` utilise `CSR/RVR/CVR` à `0xe000e010/14/18`. À 12 MHz cœur, `RVR=11999` avec CLKSOURCE=1 donne une échéance de 1 ms. Écrire CVR remet le comptage et COUNTFLAG à zéro. Configurer `CSR` avec ENABLE bit 0 et CLKSOURCE bit 2. Ajouter TICKINT bit 1 seulement après installation du vecteur 15. La lecture de CSR efface COUNTFLAG. Dans un noyau, `SVC` demande un service privilégié et `PendSV` diffère un changement de contexte. Le **MPU** applique des permissions par région. La **FPU** nécessite l'autorisation des coprocesseurs CP10/CP11 et une convention d'appel cohérente. Le projet de départ compile en `-mfloat-abi=soft`. Les compteurs et traces **DWT/ITM/ETM**, points d'arrêt et points de surveillance relèvent du débogage Arm et des capacités réellement exposées. [Guide Arm Cortex-M33](https://developer.arm.com/documentation/100235/latest/)

### 18.10 USB et HSTX

**USB 1.1 Full-Speed.** Le RP2350 peut agir en périphérique ou en hôte. Pour commencer, choisir **périphérique USB** sur la prise de la carte. Fournir `clk_usb` à 48 MHz, sortir USBCTRL du reset (28) et préparer la RAM USB dédiée à `0x50100000`. Le contrôleur est à `0x50110000`. Les adresses à inspecter en premier sont `MAIN_CTRL` à `+0x40`, `SIE_STATUS` à `+0x50` et `BUFF_STATUS` à `+0x58`. La SIE (*Serial Interface Engine*) traite les signaux et transactions USB. La DPRAM sert aux tampons et aux structures d'échange. Aligner et séparer les buffers conformément aux règles des endpoints.

```c
/* Instantané de statut. Aucun acquittement et aucune lecture de FIFO. */
static inline uint32_t usb_buffers_signales(void)
{
    return REG32(0x50110058u); // BUFF_STATUS.
}

/* Une fois le paquet SETUP d'EP0 disponible et stable dans la DPRAM. */
static inline uint16_t usb_ep0_longueur_demandee(void)
{
    const uint32_t mot = REG32(0x50100004u); // wIndex, puis wLength.
    return (uint16_t)(mot >> 16);          // USB utilise l'ordre little-endian.
}
```

Procéder par paliers. Configurer d'abord EP0, le canal de contrôle. Activer ensuite le contrôleur et la résistance de connexion. Observer le reset de bus puis les requêtes SETUP. Répondre avec les descripteurs, traiter `SET_ADDRESS` au bon moment et activer les autres endpoints après la configuration. Le statut du tampon dicte si le CPU ou le contrôleur peut le modifier. Un port série USB CDC ou un clavier HID ajoute encore un protocole de classe. Le mode hôte ajoute la gestion de VBUS (alimentation USB), des resets et de la topologie. Prévoir un montage hôte adapté au lieu de compter sur la seule prise USB de la Pico. [Registres USB du RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/master/src/rp2350/hardware_regs/include/hardware/regs/usb.h) · [RAM et structures USB](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2350/hardware_structs/include/hardware/structs/usb_dpram.h)

**HSTX.** Le transmetteur haute vitesse sérialise les mots de sa FIFO sur plusieurs sorties. Ses voies 0 à 7 sont accessibles sur GP12–19. Le contrôle est à `0x400c0000` et la FIFO à `0x50600000`. Réserver les GPIO, vérifier leur fonction HSTX (`FUNCSEL=0`), démarrer `clk_hstx`, relâcher le reset HSTX (3), puis choisir le mode et le nombre de bits émis dans `CSR` (`EN` bit 0, `SHIFT` à partir du bit 8 et `N_SHIFTS` à partir du bit 16), les sélecteurs de voies `BITn` à partir de `+4` et les registres d'expansion si besoin. Commencer avec un seul motif répété sur une voie. Ajouter le DMA seulement après une mesure à l'analyseur logique.

```c
/* Après configuration HSTX. Ne pas écrire quand STAT.FULL est à 1. */
static inline int hstx_pousser(uint32_t motif)
{
    if (REG32(0x50600000u) & BIT(8)) return 0;
    REG32(0x50600004u) = motif;
    return 1;
}
```

Un signal vidéo exige en plus la synchronisation, le codage, la fréquence et le montage électrique demandés. [Contrôle HSTX](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/hstx_ctrl.h) · [FIFO HSTX](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/hstx_fifo.h)

### 18.11 Aléa, hachage et sécurité

**TRNG.** Le générateur d'aléa matériel fournit une source d'entropie. À partir de `0x400f0000`, examiner la source à `+0x12c`, `TRNG_VALID` à `+0x110` et les mots `EHR_DATA0…5` à partir de `+0x114`. Sortir TRNG du reset (25), sélectionner/configurer la source conformément au fabricant, démarrer l'échantillonnage, attendre validité et traiter les tests de santé et erreurs. Exemple d'observation après démarrage. Lire le statut jusqu'à l'annonce d'un bloc valide, puis lire le groupe complet dans l'ordre requis par la fiche. Des mots qui changent à l'écran constituent un test d'activité. La qualité cryptographique demande une analyse et une stratégie de conditionnement. [Registres TRNG](https://raw.githubusercontent.com/raspberrypi/pico-sdk/master/src/rp2350/hardware_regs/include/hardware/regs/trng.h)

**SHA-256.** Le hachage produit un résumé de 32 octets. Le contrôleur à `0x400f8000` expose `CSR` à `+0`, `WDATA` à `+4`, `SUM0…7` à partir de `+8`. Sortir du reset (17), choisir explicitement l'ordre d'octets, initialiser via START, attendre `WDATA_RDY` avant de pousser chaque mot. Le programme doit fournir le padding SHA-256 et la longueur finale sur 64 bits. Après avoir soumis le dernier bloc, attendre `SUM_VLD`, lire les huit mots et les convertir dans l'ordre attendu. Tester avec `abc` et comparer l'empreinte avec `printf abc | sha256sum` sur le PC. SHA-256 calcule une empreinte sans chiffrement. [Registres SHA256](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/sha256.h)

```c
/* L'étape d'alimentation a déjà fourni le message et son padding. */
static inline int sha256_lire_huit_mots(uint32_t resultat[8])
{
    const uintptr_t h = 0x400f8000u;
    if (!(REG32(h) & BIT(2))) return 0; // CSR.SUM_VLD.
    for (unsigned i = 0; i < 8u; ++i)
        resultat[i] = REG32(h + 8u + 4u * i);
    return 1; // Normaliser l'ordre des octets avant de comparer.
}
```

**TrustZone, ACCESSCTRL, OTP.** TrustZone sépare les états Secure et Non-secure. `ACCESSCTRL` à `0x40060000` protège les ressources contre des maîtres ou modes d'accès non autorisés, y compris les accès DMA. Le MPU du Cortex-M33 décide des permissions dans un espace d'exécution. Pour observer, lire l'état de sécurité du cœur dans le débogueur et le registre `LOCK` à `0x40060000` dans un contexte déjà autorisé. Les alias et registres Secure ne sont pas des moyens de récupérer un accès refusé. L'OTP à `0x40120000` commande les cellules programmables une fois. L'espace `OTP_DATA` à `0x40130000` donne une vue de données avec ses règles de lecture. Commencer par une lecture documentée sur un appareil de test via l'API ROM ou `picotool otp`. Préserver les lignes de configuration, les clés et les protections d'une écriture d'exemple. Le démarrage sécurisé et les verrous irréversibles demandent un plan de production et de récupération spécifique. [ACCESSCTRL](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/accessctrl.h) · [OTP et API ROM](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)

### 18.12 Flash, caches et blocs d'infrastructure

**QMI et XIP.** QMI (*QSPI Memory Interface*) contrôle la Flash à partir de `0x400d0000`. `DIRECT_CSR` à `+0` donne notamment le contrôle des transactions directes. `0x10000000` est la fenêtre XIP (*Execute In Place*), qui permet de lire ou d'exécuter les données de Flash. La carte embarque 4 Mio. Le cache XIP et ses alias ont leur propre cartographie. Un premier test en lecture consiste à comparer quelques octets de `0x10000000` avec une lecture BOOTSEL d'une image que tu as chargée. Ne pas effectuer une transaction directe au milieu d'une lecture XIP.

Pour une écriture, rechercher les fonctions ROM de sortie/reprise XIP, effacement et programmation. Déplacer en SRAM le code concerné **avec ses constantes**, empêcher les autres cœurs, IRQ et DMA d'accéder à la Flash pendant l'opération, respecter la granularité de la puce Flash, restaurer XIP et la cohérence du cache. Sauvegarder les données avant d'allouer des secteurs. Un premier projet peut laisser l'écriture au mode BOOTSEL. [Registres QMI](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/qmi.h) · [Boot ROM](https://github.com/raspberrypi/pico-bootrom-rp2350)

| Bloc ou région | Base RP2350 | Usage et première observation |
|---|---:|---|
| SYSINFO | `0x40000000` | `CHIP_ID` à `+0`, révision dans les bits 31..28. Identifier le silicium avant de lire les errata. |
| SYSCFG | `0x40008000` | Réglages globaux des cœurs et de l'interconnexion. Consulter `PROC_CONFIG` et les autorisations avant tout changement. |
| PSM / BOOTRAM | `0x40018000` / `0x400e0000` | Séquencement d'alimentation et de démarrage. BOOTRAM est liée aux conventions ROM, dont état de boot et reprise. À observer avec leur documentation. |
| IO_QSPI / PADS_QSPI | `0x40030000` / `0x40040000` | Mux et pads de la Flash. Régler selon le circuit et la Flash, depuis un contexte SRAM sûr et sans accès XIP concurrent. |
| XOSC / ROSC / PLL | `0x40048000` / `0x400e8000` / `0x40050000`, `0x40058000` | Quartz, oscillateur interne à anneau et boucles de verrouillage de phase. Lire les statuts de stabilité/verrouillage avant de sélectionner une source. |
| BUSCTRL | `0x40068000` | Arbitrage et mesures du trafic. Observer `BUS_PRIORITY` à `+0` avant de modifier la priorité CPU/DMA. |
| XIP_CTRL / XIP_AUX | `0x400c8000` / `0x50500000` | Contrôle du cache et accès auxiliaires. Distinguer cache, alias et données Flash lors d'une mesure. |
| CoreSight et trace | `0x40140000` / `0x50700000` | Accès debug aux cœurs, corrélation des événements et trace. Partir du débogueur et de sa configuration de sonde. |
| DFT / GLITCH_DETECTOR / TBMAN | `0x40150000` / `0x40158000` / `0x40160000` | Infrastructure de test, de détection de perturbations et de testbench. Suivre les procédures de la puce pour les deux premiers. Utiliser SYSINFO.PLATFORM pour identifier l'environnement d'exécution. |
| SIO Non-secure / PPB Non-secure / EPPB | `0xd0020000` / `0xe0020000` / `0xe0080000` | Fenêtres d'accès liées au domaine ou à l'architecture. Consulter la vue de sécurité active et la cartographie avant accès. |

```c
static inline unsigned revision_rp2350(void)
{
    return (unsigned)(REG32(0x40000000u) >> 28); // SYSINFO.CHIP_ID.REVISION.
}
```

[Carte d'adresses complète du RP2350](https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/addressmap.h)

## 19. Méthode pour ajouter un pilote à ton futur OS

Pour chaque bloc, créer deux couches :

```text
rp2350_uart_regs.h  → constantes, offsets, masques ; aucun état global
uart.c / uart.h     → init, putc, getc, irq_handler ; politique de gestion
```

Écrire d'abord un test en *polling* et vérifier les registres au débogueur ; ajouter ensuite les IRQ, puis le DMA si utile. Pour un périphérique partagé entre les deux cœurs, définir explicitement qui en est propriétaire et comment la synchronisation fonctionne. Conserver dans chaque pilote une fiche indiquant les préconditions (clocks, reset, mux), les effets de bord des registres et les séquences de récupération après erreur.

**Limites de validation de cette édition :** ce complément est une explication et une carte de travail ; il ne constitue pas un ensemble de pilotes testés sur Pico 2 W. Les extraits indiquent leurs préconditions. Vérifier les états initiaux, les dépendances et les errata de la révision de la puce avant chaque mise en service.

## 20. Glossaire

| Terme | Sens dans ce guide |
|---|---|
| **bare metal, freestanding, SDK** | Le firmware démarre sur la puce et configure ses ressources. Une compilation freestanding ne suppose pas un environnement C hébergé. Un *Software Development Kit* fournit outils, bibliothèques et interfaces pour une cible. |
| **MCU, SoC, CPU, cœur, ISA** | Le microcontrôleur (*Microcontroller Unit*) est un système sur puce. Le CPU ou cœur exécute un jeu d'instructions (*Instruction Set Architecture*). Thumb est l'encodage des instructions Arm utilisé ici. |
| **ROM, SRAM, Flash, RAM** | ROM de démarrage intégrée. SRAM rapide qui perd son contenu à la coupure. Flash persistante effaçable et programmable. RAM désigne la mémoire vive. |
| **QSPI, QMI, XIP, cache** | QSPI échange sur plusieurs lignes avec la Flash. QMI pilote ces échanges. XIP expose la Flash dans l'espace mémoire. Le cache conserve des copies proches du processeur. |
| **bus, APB, AHB, MMIO, région, registre** | Un bus transporte des transactions. APB et AHB sont des interconnexions Arm. En *Memory-Mapped I/O*, une adresse représente le point d'accès à un registre matériel. |
| **base, offset, masque, bit, alias** | La base est le début d'une fenêtre. L'offset est un décalage en octets. Un masque sélectionne des bits. Un alias est une autre adresse offrant une vue ou une opération particulière. |
| **GPIO, GP, mux, pad, SIO** | Une GPIO est une entrée/sortie configurable. GP15 est son numéro logique. Le mux sélectionne le circuit qui l'utilise. Le pad règle l'interface électrique. SIO donne au CPU un chemin rapide vers des fonctions locales. |
| **ISO, IE, OD, PUE, PDE, OE** | Isolation du pad, entrée numérique active, sortie du pad désactivée, tirage haut, tirage bas, commande d'activation de sortie. Ces champs agissent à des endroits différents du trajet jusqu'à la broche. |
| **GND, AGND, VBUS, VSYS, VREF** | Masse, masse analogique, alimentation venant du bus USB, alimentation système brute et tension de référence de l'ADC. Voir le brochage avant câblage. |
| **horloge, tick, diviseur, PLL, XOSC, ROSC, LPOSC** | Une horloge cadence un bloc. Un tick est une impulsion comptée. Un diviseur ralentit le rythme. Une PLL verrouille une fréquence sur une référence. XOSC utilise le quartz, ROSC un oscillateur à anneau, LPOSC une source basse consommation. |
| **reset, POR, timeout, polling** | Remise à l'état initial, reset à l'alimentation, attente bornée et consultation répétée d'un état par le CPU. |
| **IRQ, NVIC, SCB, PPB, ISR, handler** | Demande d'interruption, contrôleur d'interruptions du cœur, bloc de contrôle système, région privée des périphériques du cœur et fonction qui traite l'événement. |
| **PC, SP, MSP, PSP, LR, xPSR, VTOR, PRIMASK** | Instruction courante ou suivante selon le contexte, pointeurs de pile général/principal/processus, registre de retour, état du processeur, adresse de la table vectorielle et masque d'interruptions. |
| **CFSR, HFSR, BFAR, MMFAR** | Statuts de fautes configurables et des HardFaults, puis adresses fautives du bus et de la mémoire lorsqu'elles sont signalées comme valides. |
| **MPU, MMU, FPU, DSP, ABI, TrustZone** | Protection mémoire par région, unité de traduction des adresses, calculateur flottant, instructions de traitement du signal, conventions binaires et états Arm Secure/Non-secure. |
| **UART, baud, 8N1, FIFO** | Série asynchrone, rythme de symboles, trame de 8 bits sans parité avec un stop et file premier entré premier sorti. |
| **SPI, SCK, MOSI, MISO, CS, CPOL, CPHA** | Bus série synchrone, horloge, fils de transfert dans chaque sens, sélection de composant, polarité de l'horloge et phase d'échantillonnage. |
| **I²C, SDA, SCL, START, ACK, NACK** | Bus à deux fils, données, horloge, début de transaction, acquittement et absence d'acquittement. |
| **PWM, ADC, DREQ, DMA, PIO, SM, HSTX** | Modulation de largeur d'impulsion, conversion analogique-numérique, requête DMA et accès direct à la mémoire, entrées/sorties programmables, machine d'état et transmetteur haute vitesse. |
| **USB, PHY, EP, EP0, SIE, DPRAM, CDC, HID** | Bus série universel, interface électrique, endpoint, endpoint de contrôle, moteur d'interface série, RAM à deux ports et classes USB de communication et d'interface humaine. |
| **SWD, DAP, GDB, CoreSight, DWT, ITM, ETM** | Lien de débogage série, port d'accès de débogage, débogueur GNU et blocs Arm de débogage, compteurs, instrumentations ou trace d'instructions. |
| **ELF, PT_LOAD, UF2, VMA, LMA, DWARF** | Format de l'exécutable, segment chargeable, fichier de transfert BOOTSEL, adresse d'exécution, adresse de chargement et métadonnées de débogage. |
| **BOOTSEL, PICOBIN, IMAGE_DEF, OTP, TRNG** | Mode de téléchargement de la ROM, format de blocs de boot RP2350, description d'image, mémoire programmable une fois et générateur d'aléa matériel. |
| **gSPI, SDPCM, NVRAM, CLM** | Bus série particulier de la radio, protocole d'échange avec son firmware, paramètres propres au module et données de limites réglementaires radio. |
| **BLE, HCI, GAP, GATT, L2CAP** | Bluetooth basse consommation, interface hôte–contrôleur, procédures de découverte/connexion, modèle d'attributs et couche logique qui transporte et multiplexe les échanges. |
| **IP, TCP, UDP, DHCP, DNS** | Adressage réseau, transport fiable connecté, datagrammes, attribution de paramètres IP et résolution de noms. |
| **AON, POWMAN, watchdog, RTOS** | Domaine toujours alimenté, contrôleur d'énergie, surveillance par minuterie et noyau pour tâches à contraintes temporelles. |
| **WFI, DMB, DSB, ISB** | Attente d'interruption, mise en ordre des accès mémoire, attente de leur achèvement et synchronisation du flux d'instructions. |

### Premier pas en C++

Les fragments sont principalement en C11. Compiler les unités `*.cpp` avec `arm-none-eabi-g++` si cette partie de la chaîne est installée. Garder les symboles du startup avec le lien C (`extern "C"`) quand ils sont définis en C++. Commencer avec des données statiques et des fonctions simples. Les exceptions, la RTTI (information de type à l'exécution) et l'allocation dynamique requièrent leur propre prise en charge. Les constructeurs globaux nécessitent un parcours d'initialisation, habituellement dans `.init_array`. Les bibliothèques et les unités du projet doivent partager la même ABI, notamment pour le flottant.

```cpp
#include <cstdint>

// Une adresse connue à la compilation. Aucun état global à initialiser.
template<std::uintptr_t adresse>
static inline volatile std::uint32_t &registre()
{
    return *reinterpret_cast<volatile std::uint32_t *>(adresse);
}

std::uint32_t numero_du_coeur()
{
    return registre<0xd0000000u>(); // SIO.CPUID.
}
```

Lire le désassemblage pour vérifier qu'un accès correspond à la lecture attendue. Le type et la syntaxe C++ ne modifient ni les droits de la transaction ni les effets de bord du registre. Une abstraction de pilote doit rendre ces effets visibles, en donnant par exemple à une opération W1C un nom explicite comme `acquitter`.
