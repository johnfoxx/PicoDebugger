# Versions

## Après v0.2.0 — extension Pico 2, 2026-11-09

- Modèles TIMER0/TIMER1 RP2350, verrou LOCKED et sélection SOURCE en temps virtuel.
- UART0/UART1 : FIFO RX/TX, loopback, états/masques/acquittements IRQ, seuils par franchissement, timeout virtuel et injection RX ; affichage TX dans la GUI.
- SPI0/SPI1 en mode maître loopback, FIFO et IRQ ; PWM à 8/12 slices avec diviseur, double buffering, wrap et IRQ.
- RESETS pour les modèles présents ; scratch WATCHDOG ; GPIO SIO propres au Pico 2 ; banques NVIC pour IRQ0..51 et SysTick RP2350. Les exceptions M33 restent explicitement absentes.
- Plusieurs petites fenêtres de périphériques, contexte cœur indépendant, fermeture sans affecter les autres, rafraîchissement après pas/reset et fermeture au changement de profil.
- Politique d'affichage Wayland sans demande d'activation, évitement du chemin de drag des docks Qt5 qui appelle `activateWindow()` ; sélection Qt5/Qt6 par CMake, version GUI issue de CMake.
- Option graphique `--chip rp2350`, diagnostic explicite du démarrage ELF direct, ELF de démonstration périphériques pour les deux puces, tests fonctionnels et graphiques étendus.
- Validation : 13 tests CTest Qt5 réussis, 11 tests hors GUI sous ASan/UBSan (fuites désactivées), parcours ELF + TX/RX dans la GUI et captures inspectées. Qt6 et Wayland natif restent non testés.

Voir [les capacités, limites et instructions d'utilisation](BOOT_AND_PERIPHERALS.md). Cette extension n'est pas une émulation complète du RP2350 ; le tag `v0.2.0` reste inchangé.

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
