# Couverture effective — extension Pico 2 après v0.2.0

L'objectif initial est un débogueur complet, fiable sur le matériel réel. **Cette première implémentation ne satisfait pas encore cet objectif de fidélité complète.** La présence d'un périphérique dans la cartographie n'implique pas son émulation.

Le profil Pico 2 réutilise actuellement le moteur Thumb du RP2040 avec quelques extensions (MOVW/MOVT, CBZ/CBNZ). Ce n'est pas un moteur ARMv8-M complet. Les ajouts de timers, UART, SPI, PWM et de descriptions de registres ne complètent ni Thumb-2 ni la gestion des exceptions M33 : une IRQ visible en attente ne signifie pas que son handler peut s'exécuter. Les mentions « partiel » et « expérimental » sont donc conservées, et le dialogue **Aide → Couverture et limites** explicite ces obstacles.

| Élément | RP2040 | RP2350 ARM |
|---|---|---|
| Deux contextes CPU/registres/piles | Oui | Oui, CPU partiel |
| SRAM partagée | 264 Kio + alias non entrelacés | 520 Kio, vue linéaire |
| Flash XIP | Lecture, 2 Mio | Lecture, 4 Mio |
| Boot ROM / boot2 / ROM multicœur | Non | Non |
| Thumb 16 bits usuels | Implémentés, validation partielle | Idem, sans IT |
| Instructions 32 bits | BL, MRS/MSR partiels, barrières | Idem + MOVW/MOVT |
| ARMv8-M, sécurité, TrustZone, MPU | Non modélisé | Non |
| FPU, DSP, accès exclusifs, division | Non | Non |
| HardFault, SVC, cadre d'exception | Modèle de base MSP/PSP | Refus explicite |
| NVIC | Enable/pending, priorités fixes | Deux banques, IRQ0..51 ; entrée d'exception refusée |
| SysTick | Temps virtuel, horloge CPU uniquement | Compteur/état virtuel ; entrée d'exception refusée |
| SIO CPUID/FIFO/spinlocks | Oui ; IRQ FIFO modélisées | Oui ; IRQ FIFO en attente, pas d'entrée M33 |
| SIO GPIO OUT/OE et SET/CLR/XOR | Latches de sortie uniquement | Latches bas/hauts aux adresses RP2350 |
| Broches externes / pinmux | Non | Non |
| Timer | Compteur 64 bits, quatre alarmes/IRQ ; temps virtuel | TIMER0/1 indépendants, LOCKED/SOURCE, alarmes/IRQ ; temps virtuel |
| UART0/1 | FIFO, loopback, TX/RX, IRQ et timeout virtuel | Idem, IRQ33/34 |
| SPI0/1 | Maître loopback, FIFO et IRQ | Idem, IRQ31/32 |
| PWM | 8 slices, compteurs/diviseur/wrap/IRQ | 12 slices, deux lignes IRQ |
| RESETS | Reset des modèles présents, complétion immédiate | Idem, bits propres au RP2350 |
| WATCHDOG | SCRATCH0..7 uniquement | SCRATCH0..7 uniquement |
| I2C/clocks/PIO/DMA/USB/ADC | Non | Non |
| Cache, contrôleur XIP, programmation Flash | Non | Non |
| RISC-V Hazard3 | Sans objet | Non |
| UF2/BIN/DWARF/démanglage C++ | Non ; ELF32 ARM + symboles | Idem |

## Instructions et exceptions

Le décodeur couvre les encodages Thumb courts de LSL/LSR/ASR/ROR, ADD/SUB/ADC/SBC/RSB, MOV/CMP/CMN, AND/EOR/ORR/BIC/MVN/TST/MUL, BX/BLX, LDR/STR mot/demi-mot/octet avec variantes signées en offset registre, ADR, arithmétique SP, PUSH/POP/LDM/STM, SXTB/SXTH/UXTB/UXTH, REV/REV16/REVSH, branchements conditionnels et inconditionnels, SVC/BKPT/UDF, CPSIE/CPSID, NOP/YIELD/WFI/WFE/SEV. CBZ/CBNZ sont activés seulement pour RP2350.

MRS/MSR couvrent les vues APSR/IPSR/xPSR, MSP/PSP, PRIMASK et CONTROL.SPSEL. Le mode non privilégié est refusé. Les barrières utilisent un modèle séquentiel sans écritures différées. Les encodages réservés/imprévisibles ne constituent pas une couverture validée, même dans une famille reconnue. IT, Thumb-2 général, FPU, DSP et TrustZone ne sont pas implémentés.

Une instruction UDF, un accès non aligné sur RP2040, un branchement non-Thumb ou une adresse hors cartographie provoquent une tentative d'entrée HardFault sur RP2040. Le cadre R0–R3/R12/LR/PC/xPSR, MSP/PSP, l'alignement et les retours `0xfffffff1`, `0xfffffff9`, `0xfffffffd` sont modélisés. Un HardFault dans HardFault provoque un lockup.

`NON ÉMULÉ` est distinct de `FAUTE CPU`. Il indique une instruction ou un périphérique hors couverture, avec arrêt jusqu'au reset. Une instruction interrompue peut avoir des effets partiels, notamment les transferts multiples ; l'état n'est pas une transaction annulée ni un état architectural certifié.

Les priorités configurables, NMI, PendSV, tail-chaining, late arrival et tous les détails des fautes d'empilement restent à implémenter. Les registres CFSR/HFSR/BFAR du Cortex-M33 ne sont pas simulés ; le diagnostic logiciel `lastFault` n'est pas présenté comme un registre matériel.

## Temps, multicœur et observation

`run` alterne les cœurs démarrés, un pas à la fois. Ce choix est reproductible, sans représenter les cycles CPU, les arbitrages du bus ni tous les entrelacements. SysTick avance d'un quantum par pas du moteur. Le timer RP2040 avance d'une microseconde **virtuelle par quantum global**, y compris pendant l'attente `WFI`/`WFE` si une alarme est programmée. Il n'avance pas lors d'une lecture de la GUI ni pendant une pause du débogueur. Les périphériques externes absents ne produisent aucun événement.

Le cœur 1 commence retenu et doit être lancé explicitement avec une entrée et un SP. Le protocole ROM/FIFO de lancement n'est pas remplacé par une heuristique. Les deux cœurs partagent la SRAM ; leurs registres système sont distincts.

La GUI lit l'état entre deux tranches synchrones. Les lectures d'inspection ne déclenchent pas de watchpoint, ne dépilent pas de FIFO, n'acquièrent pas de spinlock et n'effacent pas COUNTFLAG. La vue hexadécimale en octets ne lit pas les MMIO : utiliser l'inspecteur périphérique ou `read32(adresse)` pour les registres modélisés. Les accès impossibles s'affichent `??`/« indisponible ».

Les watchpoints s'arrêtent **après** l'instruction, y compris sur les plages qui se chevauchent et les empilements d'exception. Ils ne déclenchent pas sur le fetch des instructions. Les breakpoints s'arrêtent avant instruction. BKPT est un arrêt du débogueur, PC avancé à l'instruction suivante.

La pile affichée est brute, sans reconstruction des appels C++. Les symboles proches ne prouvent pas qu'un mot de pile soit une adresse de retour.

## Timers et inspection des périphériques

Le module `Timer` couvre les registres de `0x40054000` à `0x40054040` : écriture du temps en deux parties, capture de la partie haute lors de la lecture CPU de TIMELR, lectures brutes, alarmes auto-désarmées, ARMED/INTR W1C, INTE/INTF/INTS et PAUSE. La capture haute est partagée entre les cœurs, comme le périphérique. L'inspection ne modifie pas cette capture. Les alias XOR/SET/CLR des registres RW sont pris en charge.

Les quatre lignes IRQ alimentent les deux contextes NVIC. Un niveau maintenu ne recrée pas une interruption en attente pendant son propre handler ; il est réévalué au retour. Le modèle d'IRQ conserve les limites du NVIC actuel : priorités fixes, pas de préemption externe imbriquée ni de synchronisation cycle par cycle.

La source watchdog/horloges n'est **pas** émulée : le timer avance sans initialisation du tick matériel. DBGPAUSE accepte les bits documentés et le module peut recevoir un masque de cœurs arrêtés, mais l'ordonnanceur n'émule pas les signaux de debug matériel. La pause hôte fige tout le temps virtuel. Les accès MMIO étroits, alias W1C, délais APB et comportements réservés restent hors couverture. Les tests sont fonctionnels ; pas de comparaison sur silicium effectuée.

Le panneau **Périphériques** recense les blocs RP2040 et RP2350, y compris ceux sans modèle. Filtrage par nom, registre, champ ou adresse ; fenêtre détachable avec `Ctrl+P`. Double-cliquer sur un bloc ouvre une petite fenêtre indépendante, avec son propre contexte cœur. Les tableaux montrent adresses, valeurs, accès et champs connus. Un inspecteur dédié décode les valeurs et leur signification. ADC, I2C, DMA et PIO sont maintenant décrits sans être émulés ; les modèles TIMER0/1, UART/SPI/PWM, RESETS, SIO/PPB et scratch WATCHDOG exposent leur état disponible. Le catalogue contient 587/783 registres et 1 653/2 373 champs (RP2040/RP2350, alias compris), sans prétendre être exhaustif. USB, horloges et plusieurs autres blocs ont encore une description de registre absente ou incomplète ; aucun import SVD n'est livré.

Les particularités RP2350, la couverture exacte de chaque nouveau modèle, le démarrage ELF direct, les fenêtres UART et la correction Wayland sont détaillés dans [BOOT_AND_PERIPHERALS.md](BOOT_AND_PERIPHERALS.md). Le CPU M33 reste incomplet : rendre une IRQ visible dans NVIC ne rend pas son handler exécutable.

Les bases proviennent des références officielles figées dans [REFERENCES.md](REFERENCES.md). La cartographie présente des fenêtres de décodage, parfois avec des réserves ; elle ne certifie pas la validité de chaque adresse. Les changements de valeur sont surlignés entre rafraîchissements cohérents.

## Validation et travail restant

Avant de parler de compatibilité complète : validation différentielle instructions/flags/exceptions contre une référence indépendante et du silicium ; ROM et boot2 ; lancement multicœur ; horloges, resets et IRQ complètes ; périphériques Pico SDK ; Cortex-M33/ARMv8-M/FPU/sécurité ; tests de firmwares C++ à plusieurs niveaux d'optimisation ; modèles temporels et entrées externes.

Même après ces étapes, une réussite en simulation ne constitue pas une preuve universelle de fonctionnement matériel. Il faut tester le firmware sur sa cible avec ses périphériques et ses contraintes temporelles.
