# PicoDebugger

**PicoDebugger, ou PicoDbg, est un débogueur graphique hors ligne associé à un simulateur fonctionnel partiel des Raspberry Pi Pico et Pico W (RP2040), Pico 2 et Pico 2 W (RP2350 ARM/Thumb).** Il exécute des instructions machine sur un modèle logiciel local et permet d'observer les deux contextes CPU, la mémoire, la pile et les périphériques couverts. Aucune carte ni sonde n'est nécessaire.

Le moteur est écrit en **C++11**, indépendant de Qt. L'interface est une application de bureau **Qt Widgets**. Un programme écrit en C ou en C++ peut être chargé une fois compilé en ELF ARM compatible. Le débogueur ne compile pas les sources.

**La simulation est partielle sur les quatre cartes et les deux familles de puces.** PicoDbg n'offre actuellement ni connexion SWD/GDB à une carte, ni reproduction complète du démarrage et du comportement du silicium. Il ne peut pas exécuter un firmware Pico SDK arbitraire et ne garantit pas qu'une réussite en simulation implique un fonctionnement identique sur la cible physique. L'état affiché est celui du **modèle**, jamais une mesure d'une carte connectée.

## Commencer

- [Installation et lancement](#installation-et-lancement)
- [Synthèse du projet et contexte de reprise](SYNTHESE.md)
- [Fonctionnalités et limites](#fonctionnalités-et-limites)
- [Cartes prises en charge](#cartes-prises-en-charge)
- [Couverture d’émulation par carte](#couverture-démulation-par-carte)
- [Tutoriel rapide](#tutoriel-rapide)
- [Tutoriel complet : toutes les commandes, fonctions et cas d'usage](docs/TUTORIAL.md)
- [Organisation de l'interface et inspecteurs détachables](docs/INTERFACE.md)
- [Versions et changelogs, dont v0.5.0](docs/RELEASES.md)
- [Cartes, câblage et compatibilité ELF](docs/BOARDS.md)
- [Piloter le temps virtuel](docs/VIRTUAL_TIME.md)
- [Couverture technique détaillée](docs/COVERAGE.md)

## Installation et lancement

Prérequis : CMake ≥ 3.16, un compilateur C++11 et les fichiers de développement Qt Widgets 5.15 ou 6. Python 3 permet de générer les ELF d'exemple et d'exécuter le test d'intégration ELF. Avec Qt6, seule la couche graphique requiert C++17 ; le moteur reste en C++11.

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

La version publiée a été compilée et testée avec **Qt 5.15.15** ; Qt6 reste à valider. Une installation Qt absente provoque une erreur de configuration. `-DPICODBG_GUI=OFF` permet de compiler uniquement le moteur et ses tests, sans application graphique.

Lancer un firmware :

```sh
./build-gui/pico-debugger --chip rp2040 /chemin/firmware.elf
./build-gui/pico-debugger --board pico2_w /chemin/firmware.elf
./build-gui/pico-debugger --version
```

Le choix de carte est explicite : `--board pico|pico_w|pico2|pico2_w`. L'ancienne option `--chip rp2040|rp2350` sélectionne la carte sans Wi-Fi ; les deux options sont exclusives. La carte n'est pas déduite automatiquement des attributs de l'ELF. L'exécution commence **toujours à l'arrêt**.

## Fonctionnalités et limites

| Fonction | Ce que fournit l'application |
|---|---|
| Chargement | ELF32 ARM little-endian, lié (`ET_EXEC`), avec segments `PT_LOAD` et symboles lorsqu'ils sont présents. Pas de chargement UF2/BIN ni d'édition de liens. |
| Démarrage | Chargement direct de l'image, SP/PC issus des vecteurs reconnus ; sinon `e_entry` et sommet de SRAM, avec diagnostic explicite. Boot ROM et lancement ROM du cœur 1 absents. |
| Exécution | Pas instruction du cœur sélectionné, exécution alternée des cœurs démarrés, pause, reset/restart et démarrage manuel d'un cœur. Temps virtuel reproductible, sans précision cycle par cycle. |
| Temps virtuel | Horloge depuis reset, avance choisie en µs/ms avec firmware ou CPU figés, pause interruptible depuis la GUI. |
| Cartes | Quatre profils, câblage décrit et LED GPIO25/CYW43 distinguée ; radio et LED Wi-Fi non émulées. |
| CPU | Registres des deux cœurs côte à côte, SP/LR/PC, xPSR, MSP/PSP, CONTROL, PRIMASK, flags NZCV/T/IPSR et états retenu/sommeil/lockup. |
| Assembleur | Désassemblage Thumb, opcodes, adresses, symbole ELF proche, suivi de PC et navigation par expression. PC désigne la prochaine instruction à exécuter. |
| Mémoire et pile | Vue hexadécimale/ASCII, cartographie des régions et pile brute à partir du SP du cœur sélectionné. Pas de reconstruction des appels C++. |
| Arrêts | Breakpoints d'adresse avant instruction ; watchpoints de plage mémoire après accès CPU, en lecture, écriture ou les deux ; arrêt sur `BKPT`. |
| Expressions | Calculs entiers 32 bits, registres, adresses de symboles, lectures `read8/16/32` et déréférencement. Expressions surveillées par cœur et console `print`. Pas de variables typées, d'affectation ou d'appel de fonction cible. |
| Périphériques | Catalogue, recherche, adresses nommées, accès, valeurs disponibles, bits et significations symboliques. Inspection sans dépiler les FIFO, acquitter les IRQ ou acquérir les spinlocks. |
| Interface | Comparaison CPU, espace Code/Périphériques, inspecteurs détachables et fenêtre indépendante (`Ctrl+Maj+I`) : catalogue général ou périphérique précis, registres et bits simultanés. |
| UART | Capture des octets TX et injection de texte UTF-8 dans RX, pour les deux UART modélisés. |
| Exemples | ELF d'exemple générables sans compilateur ARM et tests d'intégration utilisant directement le moteur. |
| Source C++ | Symboles ELF seulement. Le panneau « Source C++ — prochaine itération » est un emplacement réservé : aucun import des sources, DWARF, démanglage ni pas source actuellement. |

Les tableaux d'état sont en lecture seule. Les modifications de la cible viennent des instructions exécutées, du chargement/reset, du démarrage explicite d'un cœur, de l'injection UART ou d'une avance explicite du temps. Le rafraîchissement lit un instantané cohérent entre les tranches d'exécution ; il ne constitue pas une trace de chaque instruction ou de chaque transition entre deux rafraîchissements.

### Cartes prises en charge

Les quatre cibles sont sélectionnables dans le GUI ou avec `--board`. **Toutes restent des simulations partielles**, y compris les modèles RP2040. Les capacités ci-dessous sont celles des profils du simulateur.

| Carte | Valeur de `--board` | CPU ciblé par le modèle | SRAM / Flash | LED intégrée et limite du modèle |
|---|---|---|---|---|
| Raspberry Pi Pico | `pico` | RP2040 / Cortex-M0+ | 264 Kio / 2 Mio | GPIO25 : registre de sortie observable, sans simulation électrique de la LED. |
| Raspberry Pi Pico W | `pico_w` | RP2040 / Cortex-M0+ | 264 Kio / 2 Mio | CYW43, WL_GPIO0 : LED non émulée. GPIO25 sert à la sélection radio. |
| Raspberry Pi Pico 2 | `pico2` | RP2350 / Cortex-M33 | 520 Kio / 4 Mio | GPIO25 : registre de sortie observable, sans simulation électrique de la LED. |
| Raspberry Pi Pico 2 W | `pico2_w` | RP2350 / Cortex-M33 | 520 Kio / 4 Mio | CYW43, WL_GPIO0 : LED non émulée. GPIO25 sert à la sélection radio. |

Pico W utilise le même modèle RP2040 que Pico ; Pico 2 W utilise le même modèle RP2350 que Pico 2. Leurs informations de câblage diffèrent, mais ces profils W n'ajoutent aucune émulation du CYW43, du Wi-Fi ou du Bluetooth. Le RP2350 est pris en charge uniquement en ARM/Thumb ; son mode RISC-V/Hazard3 n'est pas émulé.

### Couverture d'émulation par carte

Les statuts portent sur **la fonction précise de chaque ligne**, et non sur la conformité de toute la carte :

- **Supportée** : émulation disponible dans le modèle logiciel, dans le périmètre indiqué.
- **Partielle** : un sous-ensemble fonctionne ; des modes ou comportements matériels manquent.
- **Aucune — pas d'émulation** : fonction non émulée, même si ses registres sont décrits dans le catalogue.
- **Sans objet** : composant ou architecture absent de la carte concernée.

« Supportée » ne signifie pas reproduction cycle par cycle ni validation sur silicium. Le chargement d'un ELF compatible ne garantit pas que toutes ses instructions et tous ses accès périphériques pourront s'exécuter.

#### CPU, mémoire et démarrage

| Fonction | Pico | Pico W | Pico 2 | Pico 2 W | Commentaire / détails |
|---|---|---|---|---|---|
| Instructions CPU ARM/Thumb | Partielle | Partielle | Partielle | Partielle | Thumb court usuel, BL, MRS/MSR partiels et barrières. RP2350 : ajout de MOVW/MOVT et CBZ/CBNZ au moteur commun ; pas de moteur Cortex-M33 complet. |
| Deux contextes CPU, registres et piles | Supportée | Supportée | Supportée | Supportée | État propre à chaque cœur, SRAM commune ; les limites du jeu d'instructions restent celles de la ligne précédente. |
| Exécution multicœur | Partielle | Partielle | Partielle | Partielle | Alternance déterministe des cœurs démarrés ; cœur 1 initialement retenu, lancement manuel. Pas d'arbitrage de bus, de contention ni de démarrage ROM multicœur. |
| SRAM : lecture, écriture, exécution | Supportée | Supportée | Supportée | Supportée | 264 Kio sur RP2040 avec alias non entrelacés ; 520 Kio sur RP2350 en vue linéaire. Aucun timing d'accès matériel. |
| Flash : lecture et exécution de l'image chargée | Supportée | Supportée | Supportée | Supportée | 2 Mio sur Pico/Pico W, 4 Mio sur Pico 2/Pico 2 W ; octets fournis par l'ELF. |
| Programmation Flash, cache et contrôleur XIP | Aucune | Aucune | Aucune | Aucune | La lecture de l'image Flash ne simule ni son contrôleur, ni l'effacement, ni la programmation. |
| Entrée et retour d'exception CPU | Partielle | Partielle | Aucune | Aucune | RP2040 : modèle de base HardFault/SVC et IRQ couvertes, cadres MSP/PSP. RP2350 : entrée/retour M33 refusés ; une IRQ visible ne rend pas son handler exécutable. |
| Extensions Cortex-M33 : Thumb-2 général, IT, division, DSP, FPU, accès exclusifs, MPU, TrustZone | Sans objet | Sans objet | Aucune | Aucune | Hors couverture RP2350. Les extensions limitées citées plus haut ne constituent pas une prise en charge générale d'ARMv8-M. |
| CPU RISC-V / Hazard3 | Sans objet | Sans objet | Aucune | Aucune | Seul le mode ARM/Thumb du RP2350 est ciblé. |
| Boot ROM, boot2 et boot matériel complet | Aucune | Aucune | Aucune | Aucune | Démarrage direct depuis les vecteurs ELF ou `e_entry`, toujours à l'arrêt ; pas de démarrage sécurisé RP2350 ni de compatibilité générale Pico SDK. |
| Horloge de session et avance du temps virtuel | Supportée | Supportée | Supportée | Supportée | Avance en µs/ms avec firmware ou CPU figés, pause et reset. Un quantum représente 1 µs virtuelle ; aucune précision au cycle CPU réel. |

Le détail des instructions, fautes, exceptions, alias mémoire et hypothèses temporelles figure dans [COVERAGE.md](docs/COVERAGE.md).

#### Périphériques et composants de carte

| Fonction | Pico | Pico W | Pico 2 | Pico 2 W | Commentaire / détails |
|---|---|---|---|---|---|
| TIMER | Partielle | Partielle | Partielle | Partielle | RP2040 : un TIMER ; RP2350 : TIMER0/1 indépendants, LOCKED/SOURCE. Compteurs 64 bits, quatre alarmes par timer et IRQ ; sources d'horloge virtuelles, sans arbre d'horloge matériel. |
| UART0/1 | Partielle | Partielle | Partielle | Partielle | FIFO TX/RX, capture TX, injection RX, loopback interne, seuils/IRQ et timeout virtuel. Pas de baud physique, modem, IrDA, DMA ni simulation complète des erreurs de trame. |
| SPI0/1 | Partielle | Partielle | Partielle | Partielle | Maître Motorola 4–16 bits, FIFO et IRQ, uniquement en loopback interne. Pas d'esclave externe, de mode esclave ni de DMA. |
| PWM | Partielle | Partielle | Partielle | Partielle | 8 slices sur RP2040, 12 sur RP2350 ; compteurs, diviseur, CC/TOP bufferisés, wrap et IRQ. Pas de forme d'onde sur broche, d'entrée B ni de DREQ/DMA. |
| SIO : CPUID, FIFO inter-cœurs, spinlocks, GPIO | Partielle | Partielle | Partielle | Partielle | Identifiant de cœur, échanges FIFO, verrous et registres GPIO OUT/OE avec SET/CLR/XOR ; registres bas/hauts aux adresses RP2350. Pas de pinmux, d'entrées électriques, d'interpolateurs ni de doorbells RP2350. |
| Commande de la LED intégrée | Partielle | Aucune | Partielle | Aucune | Pico/Pico 2 : seul le latch GPIO25 est observable. Sur W, la LED dépend de WL_GPIO0 du CYW43 non émulé ; écrire GPIO25 ne la commande pas. |
| RESETS | Partielle | Partielle | Partielle | Partielle | Reset et maintien en reset des modèles TIMER/UART/SPI/PWM ; complétion immédiate, lignes initialement relâchées. Pas de reset global matériel ni de domaines d'alimentation. |
| WATCHDOG | Partielle | Partielle | Partielle | Partielle | Registres SCRATCH0..7 uniquement ; compteur, expiration et reset watchdog absents. |
| NVIC / SysTick / SCB | Partielle | Partielle | Partielle | Partielle | Enable/pending, compteur virtuel, VTOR et état propre à chaque cœur ; priorités configurables absentes. RP2350 : deux banques pour IRQ0..51, sans exécution des exceptions M33. |
| ADC / I2C0/1 / DMA / PIO | Aucune | Aucune | Aucune | Aucune | Registres et champs décrits, sans conversions ADC, transferts I2C/DMA ni exécution PIO. PIO0/1 sur RP2040 ; PIO0/1/2 sur RP2350. Valeurs courantes indisponibles. |
| USB, horloges / PLL, gestion de l'alimentation | Aucune | Aucune | Aucune | Aucune | Blocs inventoriés, descriptions incomplètes ou absentes. Pas de trafic USB, de fréquences physiques ni de simulation de l'alimentation. |
| Broches, pinmux et composants externes | Aucune | Aucune | Aucune | Aucune | Connexions de carte décrites, sans signaux électriques ni capteurs/actionneurs externes simulés. |
| CYW43, Wi-Fi et Bluetooth | Sans objet | Aucune | Sans objet | Aucune | Composant radio externe des cartes W ; câblage documenté, sans modèle de puce ni de réseau. |
| Autres blocs propres au RP2350 : HSTX, OTP, TRNG, SHA256 | Sans objet | Sans objet | Aucune | Aucune | Présence dans la cartographie uniquement ; aucun comportement émulé. |

**Une description de registre n'est pas une émulation.** Le catalogue peut afficher le nom, l'adresse et les champs d'un bloc sans fournir de valeur courante ni permettre au firmware de l'utiliser. Les autres blocs inventoriés, notamment RTC sur RP2040 et CoreSight, restent sans modèle d'exécution. Un accès ou un mode hors couverture peut arrêter l'exécution avec un diagnostic « non émulé ».

Le catalogue contient **38 blocs / 587 registres / 1 653 champs pour RP2040**, et **65 blocs / 783 registres / 2 373 champs pour RP2350**, alias DMA compris. Ces nombres comptent les descriptions, pas les fonctionnalités émulées. Les états inconnus ne sont pas remplacés par zéro ou par une valeur de reset. Le Wi-Fi, le Bluetooth et les composants externes ne sont pas simulés.

Voir [Démarrage ELF, périphériques et Qt](docs/BOOT_AND_PERIPHERALS.md) pour les limites de chaque modèle.

## Tutoriel rapide

1. Lancer `./build-gui/pico-debugger --demo`, conserver **Pico / RP2040** et **Contexte : cœur 0**. L'exemple est déjà à l'arrêt.
2. Appuyer trois fois sur **F10** : R0 vaut `1`, PC vaut `0x10000206`. Le prochain `STR` n'a pas encore écrit le compteur. Un quatrième pas écrit `1` à `0x20000000`.
3. Faire **Ctrl+R**, puis **F9**, saisir `0x10000206` et **F5** : arrêt avant cette écriture. **F10** exécute l'instruction. Supprimer ce breakpoint dans **Points d'arrêt / watchpoints**.
4. Faire **Ctrl+R**, utiliser **Débogage → Ajouter un watchpoint mémoire**, saisir `counter`, taille `4`, accès **Écriture**, puis **F5** : arrêt après la première écriture. Dans **Console** (`Ctrl+5`), `print read32(counter)` affiche `1` sans déclencher le watchpoint.
5. Ajouter `read32(counter)` avec **Ctrl+E** pour suivre sa valeur. La colonne Surveillance apparaît. Supprimer le watchpoint dans son onglet **Arrêts** pour poursuivre librement jusqu'au `BKPT` de l'exemple.
6. Faire **Ctrl+P**, puis double-cliquer **TIMER** : son inspecteur s'ouvre en onglet. Sélectionner un registre, utiliser **Bits** et les filtres. **Détacher** ouvre une fenêtre indépendante ; **Rassembler** la ramène dans l'application. La pause fige le temps virtuel.
7. Pour conserver le code visible, ouvrir **Vues → Fenêtre des périphériques** (`Ctrl+Maj+I`) ; choisir **Vue générale** ou un bloc précis. Les registres et leurs bits restent visibles ensemble.
8. Faire **Ctrl+6**, choisir une durée et **Exécuter le firmware** ou **CPU figés · périphériques seuls**, puis **Avancer**. Le second mode fait progresser les périphériques et SysTick sans exécuter une instruction ; **Pause** annule la durée restante.

Pour une démonstration Pico 2 avec UART/PWM/GPIO/timer :

```sh
python3 examples/make_peripheral_demo.py build-gui/rp2350-peripherals.elf --chip rp2350
./build-gui/pico-debugger --chip rp2350 build-gui/rp2350-peripherals.elf
```

Ouvrir **UART0**, choisir son onglet **UART · TX / RX**, puis **F5** : la sortie TX affiche `OK` et le programme atteint `BKPT #3`. Injecter `RX`, revenir à **Registres**, sélectionner `UARTDR` : la première valeur RX inspectée est `0x52`. L'inspection ne consomme pas cet octet. Ces ELF sont des exemples de démarrage direct pour le débogueur ; ils ne sont pas livrés comme images UF2 amorçables sur carte.

Le [tutoriel complet](docs/TUTORIAL.md) développe ce parcours, le multicœur, les autres vues, tous les menus/raccourcis, les expressions, les erreurs et les cas d'usage.

Un [exemple C de démarrage puis clignotement de la LED](tests/firmware/boot_blink/README.md) est disponible dans `tests/firmware/boot_blink`, avec CMake et Makefile autonomes. Il propose un ELF de startup C pour PicoDbg et une configuration Pico SDK produisant une UF2 pour Pico/Pico 2 sans Wi-Fi ; les différences entre ces deux démarrages sont documentées.

## Validation et maintenance

Le projet conserve **17 suites CTest sur Linux/Qt5 avec Python 3** après le retrait de la CLI : moteur, chargement ELF, périphériques, scénario ELF périphériques, démarrage GUI, interactions, fenêtre indépendante, quatre cartes, temps virtuel et régression du focus des listes. Les tests graphiques utilisent `offscreen`. Les vérifications de périphériques ont également été exécutées sous ASan/UBSan ; Qt6, le compositeur Wayland natif et la comparaison au silicium ne sont pas validés par ces tests.

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
| `tests/`, `examples/`, `scripts/` | Vérifications, ELF reproductibles, compilation et collecte des références. |

Documents de référence : [tutoriel](docs/TUTORIAL.md), [architecture](docs/ARCHITECTURE.md), [couverture](docs/COVERAGE.md), [versions et deltas](docs/RELEASES.md), [sources officielles](docs/REFERENCES.md). Les descriptions reposent notamment sur le Pico SDK figé à **2.2.0**. `python3 scripts/fetch-reference-docs.py --list` liste les références sans réseau ; sans `--list`, le script les télécharge sur une machine disposant d'un accès réseau.
