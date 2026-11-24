# Cartes, câblage et compatibilité des ELF

Pour le **mode réel SWD**, voir [connexion et usage de la Debug Probe](HARDWARE_DEBUG.md). Les descriptions de simulation, temps virtuel et accès sans effet de bord de ce document concernent le **mode émulation**.

La barre d'exécution propose **Pico**, **Pico W**, **Pico 2** et **Pico 2 W**. Le choix de carte est distinct de celui du microcontrôleur. Un changement de carte arrête l'exécution, vide l'image chargée et ferme les inspecteurs, même entre deux cartes utilisant la même puce.

| Carte | Puce simulée | SRAM | Flash | LED intégrée |
|---|---|---|---|---|
| Pico | RP2040 / Cortex-M0+ | 264 Kio | 2 Mio | GPIO25 |
| Pico W | RP2040 / Cortex-M0+ | 264 Kio | 2 Mio | CYW43, WL_GPIO0 |
| Pico 2 | RP2350 / Cortex-M33 partiel | 520 Kio | 4 Mio | GPIO25 |
| Pico 2 W | RP2350 / Cortex-M33 partiel | 520 Kio | 4 Mio | CYW43, WL_GPIO0 |

Dans **Vues → Carte et câblage** (`Ctrl+7`), un tableau décrit LED, alimentation, VBUS, VSYS et connexions UART/I2C/SPI par défaut du SDK. La fenêtre indépendante des périphériques propose aussi cet onglet et, pour les cartes W, une entrée **CYW43 · radio (non émulée)**.

En émulation, le voyant **LED intégrée** apparaît dans la barre d’état. Sur Pico et Pico 2, pour allumer la LED, sélectionner SIO avec `GPIO25_CTRL.FUNCSEL = 5`, activer la sortie et placer le bit 25 de `SIO.GPIO_OUT` à 1. Le pad doit autoriser la sortie. Sur Pico 2, libérer aussi l’isolation avec `PADS_BANK0.GPIO25.ISO = 0`. Le voyant affiche l’état de la sortie à chaque rafraîchissement. Pour essayer le clignotement, charger [l’exemple boot_blink](../tests/firmware/boot_blink/README.md) et lancer l’exécution avec **F5**.

Pour utiliser `IO_BANK0` et `PADS_BANK0`, effacer leurs bits dans `RESETS.RESET`, puis attendre ces mêmes bits à 1 dans `RESETS.RESET_DONE`. Les registres d’un bloc maintenu en reset conservent leurs valeurs de reset, dont `ISO = 1` sur RP2350. Le modèle couvre les GPIO utilisateur du banc 0, avec contrôle, pads, niveaux numériques SIO et interruptions par cœur.

## Ce qui diffère sur W

Les cartes W utilisent GPIO23 pour activer le CYW43, GPIO24 pour les données/IRQ, GPIO25 pour la sélection et GPIO29 pour l'horloge. La mesure VSYS partage GPIO29. La LED, le mode SMPS et la détection VBUS passent respectivement par WL_GPIO0, WL_GPIO1 et WL_GPIO2 du composant radio. Écrire dans le latch GPIO25 ne commande donc pas la LED intégrée d'une carte W.

Les adresses MMIO du **RP2040** restent celles du RP2040 pour Pico et Pico W ; même principe pour le **RP2350** sur Pico 2 et Pico 2 W. Le CYW43 est externe, sans nouvelle fenêtre MMIO ajoutée artificiellement à cette cartographie. Son câblage est décrit ; la radio, ses GPIO, sa LED, le Wi-Fi et le Bluetooth ne sont pas émulés. Les tableaux ne donnent aucune valeur fictive à ces états.

## Choisir un ELF

Choisir la carte correspondant à la compilation du firmware. Le débogueur ne peut pas identifier le câblage attendu dans un ELF ARM générique : un ELF pour Pico peut charger sur le profil Pico W sans que son utilisation de GPIO25 soit adaptée à cette carte. Les validations ELF existantes restent appliquées (format, segments, adresse d'entrée, pile), mais ne constituent pas une validation de compatibilité de carte.

Les profils W n'ajoutent pas l'émulation du pilote CYW43/PIO ni du démarrage SDK complet. Un firmware SDK Wi-Fi peut s'arrêter sur une instruction ou un accès non couvert. L'exemple `tests/firmware/boot_blink` reste réservé aux cartes **sans Wi-Fi** ; sa LED GPIO25 n'est pas réinterprétée comme une LED radio.

Pour lancer la GUI avec un profil présélectionné :

```sh
./build-gui/pico-debugger --board pico_w firmware.elf
./build-gui/pico-debugger --board pico2_w firmware.elf
```

Les valeurs acceptées sont `pico`, `pico_w`, `pico2`, `pico2_w`. L'ancienne option GUI `--chip rp2040|rp2350` sélectionne la carte sans Wi-Fi correspondante ; utiliser une seule de ces deux options.

## Références

Définitions officielles du Pico SDK, figées à **2.2.0**, consultées le 2026-11-13 : [Pico](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/pico.h), [Pico W](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/pico_w.h), [Pico 2](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/pico2.h), [Pico 2 W](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/boards/include/boards/pico2_w.h). Le modèle de carte est isolé dans `board.hpp/.cpp`, sans dupliquer le bus de la puce.
