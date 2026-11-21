# PicoDebugger et contexte de reprise

État au 21 novembre 2026 pour **v0.6.0**. Dépôt `/home/jeanrenard/PicoDebugger`.

## But et architecture

**PicoDebugger (PicoDbg)** permet de déboguer un firmware C/C++ compilé en ELF ARM, soit dans le simulateur partiel local, soit sur une vraie carte via Raspberry Pi Debug Probe / SWD, OpenOCD et GDB. La même interface couvre Pico/Pico W et Pico 2/Pico 2 W en ARM.

Le moteur est en **C++11**, indépendant de l'interface **Qt Widgets**, avec compilation CMake. `src/core/` contient le moteur, `src/qt/` l'interface et `tests/` les vérifications. Qt6 exige C++17 pour la seule couche graphique.

## Fonctionnalités réalisées

- Mode matériel : backend `GdbTarget` asynchrone, protocole GDB/MI, OpenOCD lancé par la GUI ou serveur externe, contrôle de la famille de puce et correspondance des cœurs physiques.
- Menu **Cible**, barre d'état mode/carte/connexion, chargement des symboles distinct du téléversement Flash/RAM, F5/F10/pause/reset, points matériels et lecture de l'état réel dans les vues existantes. Guide : [HARDWARE_DEBUG.md](docs/HARDWARE_DEBUG.md).
- Les registres inconnus restent indisponibles ; lectures sensibles explicites. Catalogue matériel incomplet, pas d'accès direct CYW43, pas de temps virtuel ni d'injection UART en mode réel. La liaison série de la sonde reste externe à la GUI.

Les fonctions suivantes décrivent principalement l'émulation :

- Chargement ELF32 ARM, symboles, désassemblage Thumb, pas instruction, exécution, pause et reset ; deux contextes CPU, démarrage manuel du cœur 1.
- Comparaison des registres des deux cœurs, mémoire, pile, cartographie, breakpoints, watchpoints et expressions surveillées.
- Catalogue des périphériques : recherche, registres, champs de bits et inspection sans effets de bord ; onglets détachables et rassemblables.
- Fenêtre indépendante (**Ctrl+Maj+I**) conservant l'assembleur visible : vue générale ou périphérique précis, registres et bits simultanés, contexte cœur propre, UART TX/RX.
- Temps virtuel (**Ctrl+6**) : avance en µs/ms avec firmware ou CPU figés, pause interruptible et reset. Un quantum vaut 1 µs virtuelle, sans précision au cycle matériel.
- Quatre profils : **Pico/Pico W (RP2040)** et **Pico 2/Pico 2 W (RP2350 ARM)** ; câblage accessible par **Ctrl+7**. Les variantes W partagent la cartographie MMIO de leur famille.
- Modèles partiels TIMER, UART, SPI, PWM, SIO, RESETS et NVIC/SysTick ; WATCHDOG limité aux registres SCRATCH. ADC, I2C, DMA et PIO disposent de descriptions, sans modèle d'exécution.

Le débogage se fait au niveau assembleur/symboles ELF ; l'onglet source C++ reste un emplacement réservé. La radio et la LED CYW43 ne sont pas émulées. La simulation partielle ne garantit pas l'exécution d'un firmware Pico SDK arbitraire.

## Choix utilisateur et état du travail

Priorité à l'application graphique et à l'observation simultanée du code et des périphériques. **La CLI est supprimée à la demande de l'utilisateur**, y compris l'exécutable interactif/batch et son scénario d'exemple. Le GUI, sa console d'expressions et les fonctionnalités du moteur sont conservés. Les tests ELF utilisent directement l'API C++ via `picodbg_elf_tests`, construit uniquement avec `BUILD_TESTING=ON` et non installé. Maintenir le moteur indépendant de Qt et compatible C++11.

Le tag **v0.6.0** regroupe le mode matériel SWD, le retrait de la CLI et la matrice de couverture des quatre profils. La fenêtre indépendante des périphériques, les commandes du temps virtuel et les quatre profils ont été introduits dans **d6f1f84** et inclus dans **v0.5.0**. [Changelog de la v0.6.0](docs/RELEASES.md#v0-6-0)

Validation après retrait de la CLI : compilation et **17 suites CTest réussies sous Linux/Qt5**, dont les interactions GUI hors écran et UART ; **13 suites réussies sans Qt**. Les deux tests hôtes du firmware et les scénarios boot/clignotement compilés en ARM pour Pico/Pico 2 passent également. L'ouverture d'un ELF dans le GUI et l'installation du seul exécutable graphique sont vérifiées. Qt6 et Wayland natif restent non validés.

Validation actuelle : **18 suites CTest Qt5 et 13 suites sans Qt réussies**. Validation du mode matériel : nouvelle suite `probe_protocol_and_gui` utilisant des doubles de processus GDB/OpenOCD, y compris transfert RAM/Flash, erreur de puce, threads ambigus, ressources indisponibles, déconnexion et passage entre les modes. Aucun essai sur sonde/carte physique n'a encore été effectué ; ne pas annoncer une validation matérielle. Les exécutables GDB ARM/OpenOCD sont des dépendances d'exécution optionnelles ; C++11 et Qt5 restent utilisables.

Le contrat commun `DebugTarget` est indépendant de Qt ; `Debugger` conserve la simulation et `GdbTarget` contient seulement les instantanés matériels. La GUI ne lit plus directement la mémoire de l'émulateur. Les tags existants ne sont pas modifiés par cette évolution.

## Reprendre le projet

```sh
cd /home/jeanrenard/PicoDebugger
cmake -S . -B build-gui -DCMAKE_BUILD_TYPE=Debug -DPICODBG_GUI=ON -DBUILD_TESTING=ON
cmake --build build-gui -j2
ctest --test-dir build-gui --output-on-failure
./build-gui/pico-debugger --demo
```

Références : [usage](README.md), [changelog](docs/RELEASES.md), [couverture exacte](docs/COVERAGE.md), [interface](docs/INTERFACE.md), [temps virtuel](docs/VIRTUAL_TIME.md) et [cartes](docs/BOARDS.md).
