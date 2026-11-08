# PicoDebugger

Débogueur **hors ligne**, en C++11, pour explorer des ELF32 ARM/Thumb destinés aux Pico. Le moteur est indépendant de l'interface. Une console fonctionne sans bibliothèque externe ; une interface de bureau Qt Widgets 5/6 est fournie.

**État : prototype fonctionnel à émulation partielle, pas un émulateur complet de RP2040/RP2350.** Un programme Pico SDK arbitraire ne fonctionnera généralement pas : Boot ROM, horloges, USB, PIO, DMA et de nombreuses instructions Cortex-M33 ne sont pas implémentés. Les fonctionnalités non couvertes arrêtent la session explicitement. Cette version ne garantit pas l'équivalence avec une carte réelle. Voir [la couverture précise](docs/COVERAGE.md).

## Compilation et démarrage

CMake ≥ 3.16 et un compilateur C++11 suffisent pour les tests du moteur et la console optionnelle. Pour l'utilisation du débogueur, privilégier l'interface Qt ci-dessous.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPICODBG_GUI=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/pico-debugger-cli --demo
```

Exemple de session :

```text
asm
regs
step
break 0x10000204
run 100
delete 0x10000204
watch counter 4 w
run 100
print read32(counter)
reset
unwatch
launch 1 core1_entry 0x20042000
run 1000
regs
mem counter 16
```

L'exemple contient de **véritables instructions Thumb** ; les valeurs affichées proviennent de leur exécution. Deux fonctions incrémentent deux compteurs dans la SRAM partagée. L'image démarre toujours à l'arrêt, cœur 1 retenu. `launch` démarre celui-ci explicitement, sans reproduire le protocole de la ROM.

Générer un ELF de démonstration indépendant, avec vecteurs et symboles :

```sh
python3 examples/make_demo.py build/demo.elf
./build/pico-debugger-cli --elf build/demo.elf
./build/pico-debugger-cli --demo --batch examples/session.txt
```

## Interface Qt

Sur Debian/Ubuntu, ce script installe Qt5 et les outils nécessaires, compile l'interface, lance les tests et ouvre la démonstration :

```sh
bash scripts/install-qt-debian.sh --launch
```

Lancer ce script comme utilisateur de bureau, dans un terminal disposant d'un accès administrateur via `sudo` et d'un accès réseau aux dépôts Debian. `sudo` est utilisé uniquement pour les paquets ; la compilation se fait dans `build-gui`. Si Qt est déjà installé :

```sh
bash scripts/build-gui.sh --launch
```

Installation manuelle possible avec `qtbase5-dev` **ou** `qt6-base-dev`, puis :

```sh
cmake -S . -B build-gui -DPICODBG_GUI=ON
cmake --build build-gui -j2
./build-gui/pico-debugger --demo
# ou : ./build-gui/pico-debugger /chemin/firmware.elf
```

Qt 5 conserve C++11 ; Qt 6 impose C++17 à la cible graphique. Le moteur reste en C++11. **La GUI est requise par défaut** : sans les fichiers de développement Qt, CMake s'arrête avec une erreur explicite. Un build sans interface doit être demandé avec `-DPICODBG_GUI=OFF`.

La fenêtre démarre maximisée. Les panneaux essentiels sont disposés simultanément, sans onglets qui les masquent : CPU des deux cœurs et pile à gauche, assembleur/mémoire/console au centre, cartographie/périphériques/expressions/points d'arrêt à droite. Chaque tableau reste défilable ; sur un écran réduit, les panneaux sont redimensionnables et peuvent être détachés sur un autre écran.

Le menu **Vues → Rétablir la vue d'ensemble** (`Ctrl+Maj+0`) restaure cet agencement. Le futur panneau source C++ est masqué par défaut et accessible dans Vues.

Panneaux déplaçables/redimensionnables :

- Registres des **deux cœurs côte à côte**, MSP/PSP, CONTROL, PRIMASK, xPSR et flags NZCV/T/IPSR ; changements surlignés.
- Assembleur, opcodes, symboles ELF, flèche à **PC (prochaine instruction)** ; double-clic pour basculer un breakpoint.
- Mémoire hexadécimale/ASCII et pile brute du cœur sélectionné.
- Cartographie avec couverture des régions, inspecteur des registres périphériques modélisés.
- Expressions liées explicitement à un cœur, breakpoints, watchpoints lecture/écriture, console `print` et diagnostics.
- Emplacement réservé à la correspondance C++ de la deuxième itération.

**F10** : pas instruction du cœur sélectionné. **F5** : exécuter les cœurs. **Maj+F5** : pause. **Ctrl+R** : reset/restart. **F9** : ajouter un breakpoint. Les tranches d'exécution sont courtes pour garder l'interface réactive. Toutes les vues d'une même révision proviennent du même état du modèle ; l'horodatage est celui de l'hôte, pas le temps matériel.

## Exécutables et expressions

```sh
./build/pico-debugger-cli --chip rp2040 --elf firmware.elf
./build/pico-debugger-cli --chip rp2350 --elf firmware.elf
```

Le chargeur accepte ELF32 ARM little-endian, lié (`ET_EXEC`), sans interpréteur dynamique. Il charge `PT_LOAD` à l'adresse physique/LMA. Le startup du programme reste responsable de la copie Flash→RAM de `.data`. Les vecteurs sont reconnus par les sections `.vectors`, `.isr_vector`, `.vector_table` ou les symboles `__vectors`, `__VECTOR_TABLE`.

Sans vecteurs, démarrage direct à `e_entry`, SP au sommet SRAM : ce **raccourci de simulation est annoncé**. L'entrée doit être Thumb, bit 0 à 1. Le profil CPU n'est pas détecté automatiquement à partir des attributs ARM. Les capacités Flash sont fixes : 2 Mio pour Pico, 4 Mio pour Pico 2.

Les symboles donnent des **adresses**, pas des variables C++ typées : `print counter` affiche l'adresse, `print read32(counter)` lit sa valeur. Expressions : registres, symboles, entiers 32 bits, parenthèses, `+ - * / % & | ^ << >>`, `read8/16/32(adresse)` et `*adresse`. Pas d'appel de fonction cible ni d'affectation lors des rafraîchissements. DWARF, types C++ et démanglage restent à ajouter.

`reset`/`restart` rechargent l'image et remettent la RAM et les périphériques modélisés à zéro, en conservant les points d'arrêt. C'est un reset **du modèle**, pas un reset électrique du SoC. Une nouvelle image efface les breakpoints/watchpoints mémoire. Le pas manuel ignore le breakpoint sur l'instruction courante ; `run` respecte les points avant instruction et permet de reprendre une fois au-delà du point atteint.

## Organisation et tests

```text
include/picodbg/    API du moteur sans Qt
src/core/chip.cpp   profils mémoire et registres inspectables
src/core/memory.cpp bus SRAM/Flash, SIO et PPB partiels
src/core/elf.cpp    chargeur ELF borné et symboles
src/core/thumb.cpp  décodage commun exécution/désassemblage
src/core/cpu.cpp    instructions, flags, pile, exceptions partielles
src/core/debugger.cpp ordonnancement, points d'arrêt, expressions
src/cli/           console interactive et batch
src/qt/            interface Qt Widgets
tests/             suites de vérification du moteur
scripts/           installation Qt et compilation/test de la GUI
examples/          ELF reproductible et scénario à deux cœurs
docs/              architecture et couverture
```

Tests : arithmétique/flags, décalages limites, mémoire, branchements, pile, SVC/HardFault, retours d'exception, FIFO/spinlocks, SysTick, ordonnancement, watchpoints, expressions et ELF invalides. Le parcours des 65 536 premiers demi-mots teste la **robustesse** du décodeur, pas la conformité de toutes les instructions.

```sh
cmake -S . -B build-sanitize -DPICODBG_GUI=OFF -DPICODBG_SANITIZERS=ON
cmake --build build-sanitize -j2
ctest --test-dir build-sanitize --output-on-failure
```

Si LeakSanitizer ne fonctionne pas sous `ptrace`, préfixer la dernière commande par `ASAN_OPTIONS=detect_leaks=0` : AddressSanitizer/UBSan restent actifs, mais les fuites ne sont pas contrôlées.

Validation v0.1.0 : moteur et interface **compilés avec Qt 5.15**, 10 tests CTest réussis, dont démarrage et interactions graphiques en mode `offscreen`. Une capture de la vue d'ensemble a été produite et inspectée. Qt6, la compilation croisée ARM et la comparaison sur silicium restent à valider. Voir [ARCHITECTURE.md](docs/ARCHITECTURE.md).

Quand Qt est présent, CTest ajoute un test de démarrage et un test d'interactions graphiques : visibilité simultanée des panneaux, pas, breakpoint, exécution, reset et restauration de l'agencement. Pour produire une capture lors de ce dernier test :

```sh
QT_QPA_PLATFORM=offscreen ./build-gui/picodbg_gui_tests build-gui/overview.png
```

## Références

Cartographies officielles : [RP2040](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2040/hardware_regs/include/hardware/regs/addressmap.h), [RP2350](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2350/hardware_regs/include/hardware/regs/addressmap.h). Références de validation : [datasheet RP2040](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf), [datasheet RP2350](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), [ARMv6-M](https://support.arm.com/documentation/ddi0419/c/?lang=en).
