# Versions

<a id="v0-8-0"></a>

## v0.8.0 — 2026-11-21

Changements depuis **v0.7.0** (`6240011`).

### IDE et débogage

- Lancement dans l'IDE par défaut. Le débogueur s'ouvre à la demande avec le bouton **Débogueur**, lors du chargement d'un ELF ou depuis l'option `--debugger`. Les fenêtres se retrouvent mutuellement et affichent leur état de connexion, le profil de carte et l'état d'exécution.
- Compilation et lancement à l'arrêt avec **F5** ou **Ctrl+F5** dans l'IDE. Le téléversement via l'IDE laisse aussi la carte arrêtée, en RAM ou en Flash. **F10** avance d'une instruction et **Maj+F5** met en pause depuis les deux fenêtres. Dans le débogueur, **F5** continue l'exécution.
- Correspondance bidirectionnelle entre lignes C et instructions ARM à partir des tables DWARF et de GNU Binutils. L'IDE montre le C et l'assembleur dans deux volets redimensionnables à l'arrêt. Une flèche et le turquoise suivent le PC. Le violet suit la ligne choisie dans l'éditeur, même si ses instructions occupent plusieurs plages d'adresses. La colonne **Source C** dans le débogueur permet de revenir au fichier. Le volet assembleur de l'IDE se replie pendant l'exécution continue et le tableau du débogueur peut être masqué.
- Thème gris graphite commun. Le contraste de la sélection dans l'arbre de projet, des onglets, des menus et des barres d'état est corrigé.

### Outils, sonde et exemples

- Panneau **Projet → Git…** pour initialiser un dépôt, parcourir l'état et les diffs, gérer l'index, créer des commits, lire l'historique et gérer le dépôt distant avec l'exécutable Git configuré.
- Installation locale d'ARM GCC/GDB, clangd, du Pico SDK et de ses sous-modules, CMake, Ninja, OpenOCD, picotool et pioasm selon la plateforme. Le catalogue `config/dependencies.json` décrit les sources, les versions et les contrôles d'intégrité. Les outils s'installent dans les données utilisateur de PicoDebugger. Le guide indique comment actualiser le catalogue et les chemins.
- Moniteur série indépendant dans **Cible → Moniteur série de la sonde…**. Il lit les traces et les `printf` du port **U / UART** en parallèle du SWD. Il propose le port d'une sonde reconnue, mémorise le débit, affiche les pertes de connexion et exporte le journal en texte. Qt Serial Port est facultatif sous Unix grâce au transport natif.
- Modèle C autonome pour les quatre profils Pico. Le même ELF ARM lié en SRAM se charge en émulation et via SWD en mode RAM. Le modèle Pico SDK reste disponible pour les firmwares amorçables. Les sources C, fichiers CMake, scripts de lien et routines de démarrage sont commentés en français, ainsi que les opcodes Thumb des démos et le projet de clignotement.

### Validation

- 21 tests CTest dans la configuration Linux avec Qt 5.15.15 et 13 avec le moteur seul. Les tests couvrent notamment la liaison IDE-débogueur, les couleurs d'exécution et de sélection, le démarrage arrêté, Git, la validation du catalogue et le moniteur UART sur pseudo-terminal. LSP et SWD utilisent des processus simulés.
- Modèle autonome compilé avec ARM GCC pour les quatre profils. Exemple de clignotement vérifié sur l'hôte et dans le moteur RP2040 et RP2350. Les trois ELF produits par les générateurs Python sont identiques octet pour octet après annotation. Qt6, le transport Qt Serial Port, clangd réel, la compilation Pico SDK et l'essai sur carte physique restent à valider.

<a id="v0-7-0"></a>

## v0.7.0 — 2026-11-21

Changements depuis **v0.6.0** (`60e7b3f`).

- Fenêtre IDE C indépendante. Le menu Fichier du débogueur ouvre l'IDE, qui peut réafficher la même session. L'option `--ide` ouvre directement l'éditeur.
- Assistant de projet CMake pour les quatre profils Pico. Il fournit un exemple ARM GCC pour l'émulateur ou un projet Pico SDK pour la carte. Les sources et paramètres sont accessibles également dans les projets CMake importés.
- Arborescence, onglets d'édition, création et renommage de fichiers, corbeille, coloration et indentation. Diagnostics cliquables de compilation et diagnostics, analyse statique et suggestions via clangd/LSP.
- Configuration et compilation asynchrones. La File API de CMake découvre les cibles et leurs ELF. L'IDE ouvre un ELF ARM à l'arrêt, l'exécute dans l'émulateur ou le téléverse en RAM ou Flash et le lance via la connexion SWD existante.
- Détection et réglage des outils depuis l'IDE. Le README et le guide `docs/IDE.md` documentent les installateurs officiels et la mise à jour de chaque dépendance.
- 19 tests Qt5 et 13 tests sans Qt. Création, édition, erreurs du compilateur et lancement des ELF vérifiés avec CMake et ARM GCC sur Pico et Pico 2 en émulation. LSP et SWD vérifiés avec des doubles de processus. Les essais avec clangd réel, le Pico SDK et une carte physique restent à faire.

<a id="v0-6-0"></a>

## v0.6.0 — 2026-11-21

Changements depuis **v0.5.0** (`e6c195c`).

### Débogage matériel

- Mode **Carte réelle · SWD** avec Raspberry Pi Debug Probe, OpenOCD et GDB/MI pour Pico et Pico W (RP2040), Pico 2 et Pico 2 W (RP2350 ARM). OpenOCD peut être lancé par le GUI ou fourni par un serveur externe.
- Menu **Cible** et barre d'état pour le mode, la carte, la connexion et les diagnostics. La connexion arrête la cible, vérifie la famille de puce et identifie les cœurs exposés. Les variantes W se sélectionnent manuellement.
- Chargement des symboles ELF à l'ouverture du fichier. Le téléversement explicite programme la Flash ou charge un ELF compatible en SRAM et configure son entrée RAM. La carte reste arrêtée après le transfert.
- Exécution, pas instruction, pause, reset, breakpoints et watchpoints matériels. Les vues existantes affichent les registres CPU, l'assembleur GDB, la mémoire, la pile, les expressions et les périphériques accessibles.
- Rafraîchissement à l'arrêt, lectures MMIO sensibles sur demande et affichage explicite des valeurs indisponibles. Le guide décrit le périmètre du catalogue, les accès protégés et la liaison série séparée de la sonde.
- Contrat commun `DebugTarget` en C++11 sans dépendance Qt. Le parcours GDB accepte les chemins ELF avec des espaces et des accents. Les diagnostics traitent les erreurs de connexion, de chargement et de perte de liaison.

### Distribution et documentation

- Suppression de l'exécutable autonome `pico-debugger-cli`, de ses cibles de compilation et d'installation et de son scénario d'exemple (`64b77c4`). La distribution livre l'application graphique Qt et sa console d'expressions intégrée.
- Tests ELF des périphériques et du firmware de clignotement adaptés pour appeler directement le moteur. La CI ouvre un ELF d'exemple dans le GUI.
- Matrice de couverture d'émulation par profil dans le README (`d436de0`). Le README et le [guide matériel](HARDWARE_DEBUG.md) documentent le câblage, la configuration, le parcours émulation vers carte et les limites de l'inspection.

### Validation

- **18 suites CTest sous Linux/Qt 5.15.15** et **13 suites sans Qt**. La suite `probe_protocol_and_gui` vérifie le protocole asynchrone, le transfert Flash/RAM, les erreurs de connexion, le cycle de vie des processus et le passage entre les modes avec des doubles GDB/OpenOCD.
- Les deux tests hôtes du firmware et les scénarios ELF boot/clignotement dans l'émulateur passent pour Pico et Pico 2. L'installation contient l'exécutable graphique et `--version` affiche `0.6.0`.
- Syntaxe de connexion et chemin ELF accentué vérifiés avec GDB 16.3. La validation sur sonde et carte physiques reste à réaliser.

## v0.5.0 — 2026-11-20

Delta depuis **v0.4.0** (`7f2fb30`), conservé comme jalon avant ces changements. L'implémentation est portée par le commit **d6f1f84** ; cette version finalise la documentation et le contexte de reprise.

- Fenêtre autonome des périphériques (`Ctrl+Maj+I`) : vue générale ou bloc précis, registres et bits simultanés, contexte cœur indépendant, filtres, UART TX/RX, valeurs synchronisées avec l'assembleur. Les onglets et inspecteurs détachables existants restent disponibles.
- Commandes graphiques du temps virtuel (`Ctrl+6`) : horloge de session, avance en µs/ms avec firmware ou CPU figés, durée restante, pause interruptible et remise à zéro au reset. Breakpoints, fautes et attente peuvent interrompre le mode firmware ; l'avance avec CPU figés laisse les IRQ pendantes.
- Quatre cartes : Pico, Pico W, Pico 2, Pico 2 W. Métadonnées de câblage séparées de la puce ; LED GPIO25 ou CYW43 correctement identifiée, informations accessibles avec `Ctrl+7`. La GUI accepte aussi `--board`.
- Les cartes W partagent la cartographie MMIO du SoC correspondant. Le CYW43 externe et ses GPIO/LED/radio restent non émulés ; aucune compatibilité ELF universelle ni détection automatique de carte n'est annoncée.
- Modules séparés `BoardDescription`, `BoardPanel`, `PeripheralExplorer`, `UartTerminal` et `VirtualTimePanel`. L'API moteur reste C++11 et indépendante de Qt.
- Documentation de l'interface et tutoriel actualisés ; nouveaux guides [BOARDS.md](BOARDS.md) et [VIRTUAL_TIME.md](VIRTUAL_TIME.md).
- [SYNTHESE.md](../SYNTHESE.md) rassemble le but du projet, les fonctionnalités réalisées, les choix utilisateur et les commandes de reprise.
- Validation : **17 suites CTest Linux/Qt5**, dont trois nouvelles suites pour les cartes, le temps virtuel et les interactions de session. Contrôles graphiques offscreen à 1280×800 et essai UART TX/RX dans la nouvelle fenêtre avec un ELF de démonstration RP2350 sur le profil Pico 2 W ; Qt6, Wayland natif et comparaison sur silicium non validés.
- Aucun ajout CLI dans cette évolution, conformément au choix utilisateur. La couverture d'émulation CPU et radio reste inchangée.

Compilation, 17 suites CTest et parcours GUI/UART revérifiés le **2026-11-20**. Le tag annoté **v0.5.0** reprend ce changelog.


## v0.4.0 — 2026-11-13

Delta depuis **v0.3.0** (`8975932`). Jalon conservant la refonte ergonomique et l’exemple de boot avant les prochaines évolutions.

- Comparaison des deux CPU dans un seul tableau, avec largeur calculée sur les colonnes réelles pour préserver les valeurs hexadécimales complètes.
- Espace central Code/Périphériques ; Mémoire, Pile, Console et Carte mémoire regroupées en bas. Surveillance apparaît à la demande, avec compteurs d'expressions et de points d'arrêt.
- Accueil guidé sans image, état de session plus lisible, raccourcis de navigation et commandes d'exécution compactes.
- Inspecteurs ouverts en onglets et réutilisés à l'ouverture suivante. Détachement explicite, rattachement par bouton ou fermeture de la fenêtre, rassemblement global sans perte de contexte/filtre/saisie UART.
- Duplication explicite d'un inspecteur pour comparer plusieurs contextes du même périphérique ; nettoyage des onglets et fenêtres au changement de puce.
- Navigation Registres/Bits, vue UART TX/RX dédiée, retour explicite des recherches sans résultat et thème cohérent des pages détachées.
- Modules Qt `InspectionWorkspace` et `PeripheralInspection` séparés du contrôleur et du modèle. Qt5.15 minimum ou Qt6 ; couverture d'émulation inchangée.
- Tests GUI étendus : accueil, navigation, format 1280×800, comparaison CPU, réutilisation, détachement/rassemblement, duplication et nettoyage. Guide [INTERFACE.md](INTERFACE.md) et tutoriel actualisés.

- Exemple autonome `tests/firmware/boot_blink` : application et startup C11, copie `.data`, effacement `.bss`, clignotement GPIO25, variables de suivi ; configurations ELF pour le débogueur et SDK/UF2 pour Pico/Pico 2 sans Wi-Fi, avec CMake, Makefile, tests hôtes et tutoriel.
- Validation : 14 suites CTest Qt5 et deux tests hôtes du firmware. Parcours graphique offscreen à 1280×800 et UART TX/RX vérifiés. Compilation ARM/UF2 non validée (toolchain/SDK absents) ; Qt6 et bureau Wayland natif non validés.
- Couverture CPU/périphériques inchangée : temps virtuel lié aux quanta, modèles partiels, pas de Boot ROM, DWARF ni radio Wi-Fi.
- Tag annoté `v0.4.0` : ce changelog figure également dans son annotation.

<a id="v0-3-0"></a>

## v0.3.0 — 2026-11-10

Delta depuis **v0.2.0** (`7194dac`). Cette version regroupe l'extension des modèles et fenêtres (`c25e8e9`), les descriptions de registres et corrections Qt (`7755988`), puis la préparation de version avec tutoriel complet et clarification des deux profils.

```sh
git log --oneline v0.2.0..v0.3.0
git diff --stat v0.2.0..v0.3.0
git diff v0.2.0..v0.3.0
git show --no-patch v0.3.0
```

### Ce qui change par rapport à v0.2.0

| Domaine | v0.2.0 | v0.3.0 |
|---|---|---|
| Timers Pico 2 | Registres décrits, sans modèle | TIMER0 et TIMER1 fonctionnels en temps virtuel |
| UART / SPI / PWM | Sans modèles d'exécution | Modèles partiels pour les deux puces, détaillés ci-dessous |
| Inspection périphériques | Explorateur unique détachable | Explorateur et plusieurs fenêtres indépendantes, détails des bits et filtres |
| ADC / I2C / DMA / PIO | Blocs cartographiés | Registres et champs décrits, toujours sans modèle d'exécution |
| Qt / Wayland | GUI Qt5 ; chemins internes d'activation encore présents | Choix Qt5/6, focus des listes corrigé, contournement IBus ciblé, diagnostics |
| Information utilisateur | README et notes techniques | Définition précise, tutoriel complet, profils Pico et Pico 2 explicitement « partiels » |
| Validation CTest Linux/Qt5/Python | 11 suites | 14 suites |

### Modèles et démarrage

- **RP2350 TIMER0/TIMER1** : compteurs, alarmes, IRQ, verrou LOCKED et sélection SOURCE. Le temps reste virtuel ; aucune précision de fréquence matérielle n'est ajoutée.
- **UART0/UART1 des deux puces** : FIFO RX/TX, loopback interne, états/masques/acquittements IRQ, seuils par franchissement, timeout virtuel, capture TX et injection RX. Pas de timing baud, modem ou DMA.
- **SPI0/SPI1** : mode maître Motorola en loopback interne, FIFO et IRQ. Pas de simulation d'esclave externe.
- **PWM** : 8 slices RP2040 / 12 slices RP2350, compteur/diviseur, CC/TOP bufferisés, wrap et IRQ. Pas de signal électrique externe ni d'implémentation complète des modes d'entrée/diviseur.
- **RESETS** : remise à zéro des modèles timer/UART/SPI/PWM présents ; **WATCHDOG** limité à SCRATCH0..7. Les domaines non modélisés et l'expiration du watchdog restent absents.
- **Pico 2** : correction des offsets/latches GPIO SIO, banques NVIC pour IRQ0..51 et état SysTick. Les exceptions M33 restent explicitement refusées ; une IRQ visible ne garantit pas l'exécution du handler.
- Diagnostic explicite du **démarrage ELF direct**, sans Boot ROM ; option graphique `--chip rp2040|rp2350` ; générateur `examples/make_peripheral_demo.py` pour des ELF autonomes UART/PWM/GPIO/timer sur les deux profils.

### Inspection et ergonomie

- **Plusieurs petites fenêtres de périphériques**, ouvertes par double-clic ou **↗ Fenêtre** ; déplacement/redimensionnement indépendants, contexte cœur propre, fermeture sans affecter les autres. Elles se rafraîchissent après pas/reset et se ferment au changement de puce.
- Tableaux de registres et de **champs de bits** : positions, valeurs et significations symboliques, notamment UART/SPI ; recherche par registre/champ/adresse et détails repliables.
- Descriptions **ADC, I2C, DMA et PIO** pour les deux puces, avec adresses et champs propres au RP2350. Catalogue livré : RP2040 **38 blocs / 587 registres / 1 653 champs** ; RP2350 **65 / 783 / 2 373**, alias DMA compris. Ce sont des descriptions, sans valeurs inventées pour les blocs non émulés.
- Surlignage des changements, partage de l'instantané avec les autres vues et inspection sans effets de bord ; aucune lecture MMIO supplémentaire pour décoder les bits.
- Affichage de la capture **UART TX** et saisie pour **injecter RX** dans la fenêtre UART ; agencement principal compact et détails repliables.

### Qt et maintenance

- Affichage des fenêtres sans demande d'activation ; évitement sous Qt5/Wayland du détachement par glisser-déposer dont le chemin interne appelle `activateWindow()`. **Ctrl+P** et les fenêtres indépendantes restent disponibles.
- Correction du chemin de fermeture des listes Qt5 : retour du focus différé jusqu'au focus réel de leur fenêtre. Sélecteurs de puce/cœur et dialogues concernés ; les avertissements ne sont pas filtrés.
- La trace utilisateur a localisé **QSocketNotifier** dans l'initialisation IBus avant le dispatcher Qt5/Wayland. Le contournement, limité au processus et au cas `QT_IM_MODULE=ibus`, laisse Qt choisir sa saisie Wayland native avec repli `compose`. `--qt-system-input` préserve la sélection système.
- **`--qt-diagnostics`** affiche versions Qt, plateforme, saisie et pile des avertissements ciblés dès le démarrage ; aucun réglage du bureau n'est modifié.
- Choix **Qt5/Qt6** via `PICODBG_QT_MAJOR`, version GUI issue de CMake. Le moteur reste C++11 ; seule la GUI Qt6 requiert C++17.
- Séparation du bus périphérique, des modèles série/PWM, des descriptions par famille et du composant Qt d'inspection des registres.
- **Version CMake 0.3.0** ; menu et noms de profils harmonisés en « partiel » pour RP2040 et RP2350, avec infobulles expliquant les différences CPU.
- [README](../README.md) réécrit : définition exacte du logiciel, fonctions disponibles et limites. Nouveau [tutoriel complet](TUTORIAL.md) : menus/raccourcis, pas, arrêts, expressions, mémoire/pile, multicœur, fenêtres de périphériques, UART, chargement ELF, dépannage Qt, CLI et cas d'usage.

### Validation

- **14 suites CTest Linux / Qt 5.15.15 / Python 3**, exécutées sur la version préparée. Nouvelles suites depuis v0.2.0 : `devices`, `elf_peripheral_boot`, `qt_popup_activation` ; tests graphiques existants étendus. GUI testée en `offscreen`.
- Vérification des exemples du tutoriel : pas et compteur, breakpoint avant écriture, watchpoint après écriture, scénario multicœur et ELF périphériques des deux profils. La GUI annonce **PicoDebugger 0.3.0**.
- Pendant les commits inclus dans cette version : 11 suites hors GUI sous ASan/UBSan et 11 017 vérifications de la suite périphériques, détection des fuites désactivée ; parcours ELF UART TX/RX dans la GUI et captures inspectées.
- **Qt6, le compositeur Wayland natif et la conformité au silicium ne sont pas validés** par ces contrôles. Les tests de focus sous `offscreen` ne prouvent pas la disparition de tout avertissement sur un bureau Wayland réel.

### Ce qui n'est pas nouveau ou reste absent

Le chargement ELF, les deux contextes CPU, les vues registres/mémoire/pile/assembleur, les breakpoints/watchpoints et les expressions existaient déjà dans v0.2.0. MOVW/MOVT et CBZ/CBNZ ne sont pas des ajouts CPU de cette version.

**Aucun des deux profils n'est complet.** Le RP2040 possède un modèle d'exceptions de base plus avancé ; le RP2350 n'a toujours pas de moteur ARMv8-M complet, de Thumb-2 général, de FPU, de TrustZone ou d'entrées d'exception M33. Boot ROM, nombreux périphériques, accès SWD/GDB à une carte, source C++/DWARF, simulation Wi-Fi et garantie d'exécution de tout firmware SDK restent absents. Voir [la couverture](COVERAGE.md) et [les modèles de périphériques](BOOT_AND_PERIPHERALS.md).

Le tag **annoté `v0.3.0`** porte ce delta ; `v0.2.0` et `v0.1.0` restent inchangés. Commit et tag locaux : aucun dépôt distant n'est configuré.

## v0.2.0 — 2026-11-08

Liste des ajouts et corrections depuis **v0.1.0** (`8bb5af8`). Comparaison Git : `git diff v0.1.0..v0.2.0` ; historique : `git log --oneline v0.1.0..v0.2.0`. L'implémentation est portée par le commit `3b97877`, suivi du commit de préparation de cette version.

### Interface et inspection

- Nouvel explorateur des blocs matériels RP2040/RP2350, incluant les périphériques non émulés avec une indication explicite de leur couverture.
- Recherche par nom de périphérique, nom de registre ou adresse ; tableau des registres décrits avec valeur, adresse, mode d'accès, champs décodés et description.
- Fenêtre des périphériques détachable avec **Ctrl+P**, utilisable sur un autre écran ; retour dans l'agencement principal avec **Ctrl+Maj+0**.
- Valeurs actualisées au même état du modèle que les autres vues, contexte du cœur sélectionné et surlignage des changements. L'inspection ne modifie ni FIFO, ni spinlocks, ni capture haute du timer.
- Descriptions distinctes des registres TIMER0/TIMER1 du RP2350, sans valeurs simulées pour ces périphériques encore non émulés.
- Correction de l'affichage tronqué des registres CPU : largeur préservant les valeurs hexadécimales complètes, avec défilement si nécessaire. Correction du contraste des panneaux détachés.

### Moteur et cartographie

- Nouveau modèle fonctionnel du timer RP2040 : compteur 64 bits, écriture du temps en deux parties, lecture haute mémorisée et lectures brutes.
- Quatre alarmes, comparaison sur les 32 bits bas avec gestion du débordement, armement/désarmement, acquittement W1C et registres INTR/INTE/INTF/INTS.
- Prise en charge de PAUSE, des bits DBGPAUSE et des alias XOR/SET/CLR des registres RW couverts. Les alias non validés restent refusés.
- Progression d'une microseconde virtuelle par quantum global ; poursuite de l'ordonnancement pendant WFI/WFE lorsqu'une alarme est programmée.
- Routage des IRQ timer vers les deux contextes NVIC. Suivi des interruptions externes actives pour éviter un redéclenchement artificiel pendant leur handler et réévaluer un niveau encore présent au retour.
- Cartographie RP2350 détaillée : fenêtres OTP, CoreSight, DFT, GLITCH_DETECTOR, TBMAN, HSTX_FIFO, trace CoreSight, SIO/PPB non sécurisés, EPPB et SRAM/cache XIP.
- Modules séparés pour le comportement du timer (`Timer`), les métadonnées des périphériques et leur panneau Qt (`PeripheralPanel`). Le moteur reste indépendant de Qt et en C++11.

### Documentation et validation

- [Références officielles](REFERENCES.md) regroupées : datasheets, manuels ARM, Boot ROM et en-têtes du SDK figés à la version 2.2.0 ; choix de modélisation et étapes de conformité documentés.
- Script `scripts/fetch-reference-docs.py` pour récupérer les PDF et en-têtes : contrôle de format/taille, cache ignoré par Git, manifeste URL/date/taille/SHA-256 et option de rafraîchissement. La récupération locale n'a pas abouti dans cette session à cause du DNS ; aucun PDF n'est inclus dans la version.
- Nouvelle suite `peripherals` : latches, alarmes, débordement, pause, alias, IRQ, attente WFI et cohérence de la cartographie.
- Tests Qt étendus au catalogue, filtre, valeurs du timer, fenêtre détachée, changement de profil RP2040/RP2350 et contexte cœur. Captures de la vue d'ensemble et de la fenêtre des périphériques inspectées.
- **11 tests CTest réussis avec Qt5**, contre 10 dans v0.1.0 ; les **neuf suites moteur passent sous ASan/UBSan**, avec détection des fuites désactivée. Version du projet CMake portée à 0.2.0.

### Limites conservées

L'émulation reste partielle : le timer utilise un temps virtuel sans source watchdog/horloges matérielles ; Boot ROM, nombreux périphériques et Cortex-M33 complet restent à réaliser. Le catalogue des blocs n'est pas une description exhaustive de leurs registres. Aucune conformité au silicium ni compatibilité universelle des firmwares Pico SDK n'est annoncée. Voir [COVERAGE.md](COVERAGE.md).

Le tag annoté `v0.2.0` identifie cette version ; `v0.1.0` reste inchangé. Les commits et tags sont locaux, aucun dépôt distant n'est configuré.

## v0.1.0 — 2026-11-08

Premier jalon graphique : moteur C++11 ARM/Thumb partiel, ELF32, deux contextes CPU, pas/reset/breakpoints/watchpoints, inspection simultanée CPU/mémoire/pile/assembleur et périphériques modélisés. Interface Qt5 compilée ; 10 tests CTest réussis, y compris les interactions GUI offscreen.

Ce tag préserve la base avant l'extension des périphériques. La simulation complète des RP2040/RP2350, les programmes Pico SDK arbitraires et l'équivalence avec la cible physique ne sont pas acquis ; voir COVERAGE.md. Le tag est local, aucun dépôt distant n'est configuré.
