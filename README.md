# PicoDebugger — v0.3.0

**PicoDebugger, ou PicoDbg, est un débogueur graphique hors ligne associé à un simulateur fonctionnel partiel des Raspberry Pi Pico/RP2040 et Pico 2/RP2350 en mode ARM/Thumb.** Il exécute des instructions machine sur un modèle logiciel local et permet d'observer les deux contextes CPU, la mémoire, la pile et les périphériques couverts. Aucune carte ni sonde n'est nécessaire.

Le moteur est écrit en **C++11**, indépendant de Qt. L'interface principale est une application de bureau **Qt Widgets** ; une CLI complémentaire sert aux scénarios automatisés. Un programme écrit en C ou en C++ peut être chargé une fois compilé en ELF ARM compatible. Le débogueur ne compile pas les sources.

**La simulation est partielle sur les deux profils.** PicoDbg n'offre actuellement ni connexion SWD/GDB à une carte, ni reproduction complète du démarrage et du comportement du silicium. Il ne peut pas exécuter un firmware Pico SDK arbitraire et ne garantit pas qu'une réussite en simulation implique un fonctionnement identique sur la cible physique. L'état affiché est celui du **modèle**, jamais une mesure d'une carte connectée.

## Commencer

- [Installation et lancement](#installation-et-lancement)
- [Fonctionnalités et limites](#fonctionnalités-et-limites)
- [Tutoriel rapide](#tutoriel-rapide)
- [Tutoriel complet : toutes les commandes, fonctions et cas d'usage](docs/TUTORIAL.md)
- [Delta exact v0.2.0 → v0.3.0](docs/RELEASES.md#v0-3-0)
- [Couverture technique détaillée](docs/COVERAGE.md)

## Installation et lancement

Prérequis : CMake ≥ 3.16, un compilateur C++11 et les fichiers de développement Qt Widgets 5 ou 6. Python 3 permet de générer les ELF d'exemple et d'exécuter le test d'intégration ELF. Avec Qt6, seule la couche graphique requiert C++17 ; le moteur reste en C++11.

Depuis la racine du dépôt, avec Qt déjà installé :

```sh
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Debug -DPICODBG_GUI=ON -DBUILD_TESTING=ON
cmake --build build-gui -j2
ctest --test-dir build-gui --output-on-failure
./build-gui/pico-debugger --demo
```

Le script `bash scripts/build-gui.sh --launch` réalise ces opérations. Sur Debian/Ubuntu, `bash scripts/install-qt-debian.sh --launch` installe aussi Qt5 et les outils nécessaires ; il requiert un accès réseau et `sudo` pour les paquets. Il se lance avec l'utilisateur du bureau, pas dans une session graphique exécutée en administrateur.

CMake accepte `-DPICODBG_QT_MAJOR=AUTO`, `5` ou `6`. `AUTO` préfère Qt6 si disponible, sinon Qt5. Pour imposer Qt6, utiliser un dossier distinct :

```sh
cmake -S . -B build-qt6 -DPICODBG_GUI=ON -DPICODBG_QT_MAJOR=6
cmake --build build-qt6 -j2
```

La version publiée a été compilée et testée avec **Qt 5.15.15** ; Qt6 reste à valider. Une installation Qt absente provoque une erreur de configuration, sans remplacement silencieux de la GUI par la CLI.

Lancer un firmware :

```sh
./build-gui/pico-debugger --chip rp2040 /chemin/firmware.elf
./build-gui/pico-debugger --chip rp2350 /chemin/firmware.elf
./build-gui/pico-debugger --version
```

Le choix de puce est explicite : il n'est pas déduit automatiquement des attributs de l'ELF. L'exécution commence **toujours à l'arrêt**.

## Fonctionnalités et limites

| Fonction | Ce que fournit v0.3.0 |
|---|---|
| Chargement | ELF32 ARM little-endian, lié (`ET_EXEC`), avec segments `PT_LOAD` et symboles lorsqu'ils sont présents. Pas de chargement UF2/BIN ni d'édition de liens. |
| Démarrage | Chargement direct de l'image, SP/PC issus des vecteurs reconnus ; sinon `e_entry` et sommet de SRAM, avec diagnostic explicite. Boot ROM et lancement ROM du cœur 1 absents. |
| Exécution | Pas instruction du cœur sélectionné, exécution alternée des cœurs démarrés, pause, reset/restart et démarrage manuel d'un cœur. Temps virtuel reproductible, sans précision cycle par cycle. |
| CPU | Registres des deux cœurs côte à côte, SP/LR/PC, xPSR, MSP/PSP, CONTROL, PRIMASK, flags NZCV/T/IPSR et états retenu/sommeil/lockup. |
| Assembleur | Désassemblage Thumb, opcodes, adresses, symbole ELF proche, suivi de PC et navigation par expression. PC désigne la prochaine instruction à exécuter. |
| Mémoire et pile | Vue hexadécimale/ASCII, cartographie des régions et pile brute à partir du SP du cœur sélectionné. Pas de reconstruction des appels C++. |
| Arrêts | Breakpoints d'adresse avant instruction ; watchpoints de plage mémoire après accès CPU, en lecture, écriture ou les deux ; arrêt sur `BKPT`. |
| Expressions | Calculs entiers 32 bits, registres, adresses de symboles, lectures `read8/16/32` et déréférencement. Expressions surveillées par cœur et console `print`. Pas de variables typées, d'affectation ou d'appel de fonction cible. |
| Périphériques | Catalogue, recherche, adresses nommées, accès, valeurs disponibles, bits et significations symboliques. Inspection sans dépiler les FIFO, acquitter les IRQ ou acquérir les spinlocks. |
| Fenêtres | Panneaux simultanés, redimensionnables ; fenêtres indépendantes de périphériques, contexte cœur propre, détails des bits repliables. |
| UART | Capture des octets TX et injection de texte UTF-8 dans RX, pour les deux UART modélisés. |
| Automatisation | CLI interactive ou batch avec commandes d'exécution et d'inspection ; ELF d'exemple générables sans compilateur ARM. |
| Source C++ | Symboles ELF seulement. Le panneau « Source C++ — prochaine itération » est un emplacement réservé : aucun import des sources, DWARF, démanglage ni pas source actuellement. |

Les tableaux d'état sont en lecture seule. Les modifications de la cible viennent des instructions exécutées, du chargement/reset, du démarrage explicite d'un cœur ou de l'injection UART. Le rafraîchissement lit un instantané cohérent entre les tranches d'exécution ; il ne constitue pas une trace de chaque instruction ou de chaque transition entre deux rafraîchissements.

### Pourquoi les deux profils sont-ils « partiels » ?

L'ancien menu qualifiait seulement le Pico 2 d'« expérimental » parce que son moteur M33 avait davantage de lacunes. Cela ne signifiait pas que le Pico 1 était complet. **v0.3.0 affiche « partiel » pour les deux profils**, avec des infobulles et une aide détaillant leurs différences.

| Couverture CPU | Pico / RP2040 | Pico 2 / RP2350 |
|---|---|---|
| Deux contextes et SRAM partagée | Oui, 264 Kio de SRAM | Oui, 520 Kio de SRAM |
| Flash du profil | 2 Mio, lecture XIP | 4 Mio, lecture XIP |
| Instructions | Thumb court usuel, BL, MRS/MSR partiels et barrières | Même socle, plus MOVW/MOVT et CBZ/CBNZ |
| Exceptions | Modèle de base HardFault/SVC, empilement et retour | Entrée d'exception M33 explicitement refusée |
| Limites supplémentaires M33 | Sans objet pour ces extensions | Thumb-2 général, IT, division, DSP, FPU, accès exclusifs, MPU et TrustZone absents |

Les deux profils partagent ces limites : Boot ROM, horloges et nombreux périphériques absents, NVIC et timing incomplets, absence de validation de conformité au silicium. Le détail des instructions, fautes, exceptions, alias mémoire et hypothèses temporelles figure dans [COVERAGE.md](docs/COVERAGE.md). Le RP2350 RISC-V/Hazard3 n'est pas pris en charge.

### Périphériques : description et émulation sont distinctes

| Famille | État du modèle |
|---|---|
| TIMER RP2040 ; TIMER0/1 RP2350 | Compteurs, alarmes, masques/forçages IRQ, temps virtuel ; LOCKED/SOURCE sur RP2350. |
| UART0/1 | FIFO, TX/RX, loopback interne, seuils/IRQ, timeout virtuel ; pas de baud physique, modem ni DMA. |
| SPI0/1 | Maître Motorola en loopback interne, FIFO et IRQ ; pas d'esclave externe simulé. |
| PWM | 8/12 slices, compteurs/diviseur, CC/TOP bufferisés, wrap et IRQ ; pas de forme d'onde sur des broches externes. |
| SIO | CPUID, FIFO inter-cœurs, spinlocks et latches GPIO de sortie ; pas de pinmux ni de signaux électriques. |
| RESETS / WATCHDOG | Reset des modèles présents ; WATCHDOG limité aux SCRATCH0..7, sans expiration/reset watchdog. |
| NVIC / SysTick | Sous-ensemble d'état et temps virtuel ; afficher une IRQ RP2350 ne rend pas son handler M33 exécutable. |
| ADC / I2C / DMA / PIO | Registres et champs décrits, **sans modèle d'exécution** ; valeurs courantes indisponibles. |
| Autres blocs | Inventoriés ; descriptions et modèles incomplets ou absents, notamment USB, horloges et contrôleurs Flash/cache. |

Le catalogue contient **38 blocs / 587 registres / 1 653 champs pour RP2040**, et **65 blocs / 783 registres / 2 373 champs pour RP2350**, alias DMA compris. Ces nombres comptent les descriptions, pas les fonctionnalités émulées. Les états inconnus ne sont pas remplacés par zéro ou par une valeur de reset. Le Wi-Fi, le Bluetooth et les composants externes ne sont pas simulés.

Voir [Démarrage ELF, périphériques et Qt](docs/BOOT_AND_PERIPHERALS.md) pour les limites de chaque modèle.

## Tutoriel rapide

1. Lancer `./build-gui/pico-debugger --demo`, conserver **Pico / RP2040 (partiel)** et **Contexte : cœur 0**. L'exemple est déjà à l'arrêt.
2. Appuyer trois fois sur **F10** : R0 vaut `1`, PC vaut `0x10000206`. Le prochain `STR` n'a pas encore écrit le compteur. Un quatrième pas écrit `1` à `0x20000000`.
3. Faire **Ctrl+R**, puis **F9**, saisir `0x10000206` et **F5** : arrêt avant cette écriture. **F10** exécute l'instruction. Supprimer ce breakpoint dans **Points d'arrêt / watchpoints**.
4. Faire **Ctrl+R**, cliquer **+ Watch**, saisir `counter`, taille `4`, accès **Écriture**, puis **F5** : arrêt après la première écriture. La console graphique accepte `print read32(counter)` ; elle affiche `1` sans déclencher le watchpoint.
5. Ajouter `read32(counter)` avec **+ Expression** pour suivre sa valeur. Supprimer le watchpoint pour poursuivre librement jusqu'au `BKPT` de l'exemple.
6. Double-cliquer sur **TIMER** dans le catalogue : sa fenêtre indépendante montre l'état du modèle. Sélectionner un registre, utiliser **Bits** et les filtres. La pause du débogueur fige le temps virtuel.

Pour une démonstration Pico 2 avec UART/PWM/GPIO/timer :

```sh
python3 examples/make_peripheral_demo.py build-gui/rp2350-peripherals.elf --chip rp2350
./build-gui/pico-debugger --chip rp2350 build-gui/rp2350-peripherals.elf
```

Ouvrir **UART0**, puis **F5** : la sortie TX affiche `OK` et le programme atteint `BKPT #3`. Injecter `RX`, sélectionner `UARTDR` : la première valeur RX inspectée est `0x52`. L'inspection ne consomme pas cet octet. Ces ELF sont des exemples de démarrage direct pour le débogueur ; ils ne sont pas livrés comme images UF2 amorçables sur carte.

Le [tutoriel complet](docs/TUTORIAL.md) développe ce parcours, le multicœur, les autres vues, tous les menus/raccourcis, les expressions, les erreurs, la CLI et les cas d'usage.

## Validation et maintenance

v0.3.0 comporte **14 suites CTest sur Linux/Qt5 avec Python 3** : moteur, chargement ELF, périphériques, scénario ELF périphériques, démarrage GUI, interactions et régression du focus des listes. Les tests graphiques utilisent `offscreen`. Les vérifications de périphériques ont également été exécutées sous ASan/UBSan ; Qt6, le compositeur Wayland natif et la comparaison au silicium ne sont pas validés par ces tests.

```sh
cmake -S . -B build-sanitize -DPICODBG_GUI=OFF -DPICODBG_SANITIZERS=ON
cmake --build build-sanitize -j2
ctest --test-dir build-sanitize --output-on-failure
```

Si l'environnement empêche LeakSanitizer de fonctionner, utiliser `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure` : ASan/UBSan restent actifs, mais les fuites ne sont pas vérifiées.

| Emplacement | Responsabilité |
|---|---|
| `include/picodbg/`, `src/core/` | API C++11, CPU, décodeur Thumb, ELF, mémoire, ordonnanceur et modèles de périphériques. |
| `src/core/registers/` | Descriptions matérielles par famille, indépendantes du comportement des modèles. |
| `src/qt/` | Fenêtre principale, inspection des périphériques/bits et compatibilité Qt. |
| `src/cli/` | Interface interactive et batch. |
| `tests/`, `examples/`, `scripts/` | Vérifications, ELF reproductibles, compilation et collecte des références. |

Documents de référence : [tutoriel](docs/TUTORIAL.md), [architecture](docs/ARCHITECTURE.md), [couverture](docs/COVERAGE.md), [versions et deltas](docs/RELEASES.md), [sources officielles](docs/REFERENCES.md). Les descriptions reposent notamment sur le Pico SDK figé à **2.2.0**. `python3 scripts/fetch-reference-docs.py --list` liste les références sans réseau ; sans `--list`, le script les télécharge sur une machine disposant d'un accès réseau.
