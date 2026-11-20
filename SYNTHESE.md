# PicoDebugger et contexte de reprise

État au 20 novembre 2026, version **v0.5.0**. Dépôt : `/home/jeanrenard/PicoDebugger`.

## But et architecture

**PicoDebugger (PicoDbg)** permet de déboguer et comprendre un firmware C/C++ compilé en ELF ARM, sans carte physique ni sonde. Il associe une interface graphique à une simulation partielle des Raspberry Pi Pico.

Le moteur est en **C++11**, indépendant de l'interface **Qt Widgets**, avec compilation CMake. `src/core/` contient le moteur, `src/qt/` l'interface et `tests/` les vérifications. Qt6 exige C++17 pour la seule couche graphique.

## Fonctionnalités réalisées

- Chargement ELF32 ARM, symboles, désassemblage Thumb, pas instruction, exécution, pause et reset ; deux contextes CPU, démarrage manuel du cœur 1.
- Comparaison des registres des deux cœurs, mémoire, pile, cartographie, breakpoints, watchpoints et expressions surveillées.
- Catalogue des périphériques : recherche, registres, champs de bits et inspection sans effets de bord ; onglets détachables et rassemblables.
- Fenêtre indépendante (**Ctrl+Maj+I**) conservant l'assembleur visible : vue générale ou périphérique précis, registres et bits simultanés, contexte cœur propre, UART TX/RX.
- Temps virtuel (**Ctrl+6**) : avance en µs/ms avec firmware ou CPU figés, pause interruptible et reset. Un quantum vaut 1 µs virtuelle, sans précision au cycle matériel.
- Quatre profils : **Pico/Pico W (RP2040)** et **Pico 2/Pico 2 W (RP2350 ARM)** ; câblage accessible par **Ctrl+7**. Les variantes W partagent la cartographie MMIO de leur famille.
- Modèles partiels TIMER, UART, SPI, PWM, SIO, RESETS et NVIC/SysTick ; WATCHDOG limité aux registres SCRATCH. ADC, I2C, DMA et PIO disposent de descriptions, sans modèle d'exécution.

Le débogage se fait au niveau assembleur/symboles ELF ; l'onglet source C++ reste un emplacement réservé. La radio et la LED CYW43 ne sont pas émulées. La simulation partielle ne garantit pas l'exécution d'un firmware Pico SDK arbitraire.

## Choix utilisateur et état du travail

Priorité à l'application graphique et à l'observation simultanée du code et des périphériques. **Les ajouts CLI ont été abandonnés à la demande de l'utilisateur** ; la CLI existante est conservée. Maintenir le moteur indépendant de Qt et compatible C++11.

Dernière tâche terminée : fenêtre indépendante, temps virtuel et quatre cartes, implémentés dans **d6f1f84** et inclus dans **v0.5.0**. **v0.4.0** conserve l'état précédent. Aucune nouvelle tâche fonctionnelle n'est engagée.

Validation du 20 novembre : compilation et **17 suites CTest réussies sous Linux/Qt5**, interactions GUI hors écran et UART vérifiés. Qt6 et Wayland natif restent non validés.

## Reprendre le projet

```sh
cd /home/jeanrenard/PicoDebugger
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Debug -DPICODBG_GUI=ON -DBUILD_TESTING=ON
cmake --build build-gui -j2
ctest --test-dir build-gui --output-on-failure
./build-gui/pico-debugger --demo
```

Références : [usage](README.md), [changelog](docs/RELEASES.md), [couverture exacte](docs/COVERAGE.md), [interface](docs/INTERFACE.md), [temps virtuel](docs/VIRTUAL_TIME.md) et [cartes](docs/BOARDS.md).
