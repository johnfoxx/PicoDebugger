# Boot puis clignotement — Pico / Pico 2, en C

Petit projet autonome pour **Pico (RP2040)** et **Pico 2 (RP2350 ARM)** sans Wi-Fi. La LED intégrée est sur **GPIO25**, active à l'état haut. Par défaut : **500 ms allumée, 500 ms éteinte**. Le cœur 1 reste inutilisé.

L'application et tous les fichiers de startup ajoutés ici sont en **C11**, sans assembleur écrit à la main. Le firmware destiné à la carte utilise le code bas niveau du Pico SDK, qui contient lui-même de l'assembleur. Le Pico W/Pico 2 W utilise une LED reliée au composant radio : ces cartes sont explicitement refusées par cet exemple.

## Deux configurations, une application C

| Configuration | Startup et périphériques | Résultat |
|---|---|---|
| `debugger` — défaut | Vecteurs, copie `.data`, effacement `.bss`, VTOR, `main` en C ; SIO et timer du modèle | `boot_blink.elf` à charger dans PicoDbg. **Pas une image amorçable sur carte.** |
| `board` | Boot ROM puis startup/runtime officiel du SDK, configuration réelle du GPIO et temporisation SDK | `boot_blink.uf2` à copier sur la carte via BOOTSEL ; ELF également produit pour les outils matériels |

La boucle de clignotement et les variables d'inspection sont communes. Seules les fonctions de `platform.h` changent. **L'ELF SDK n'est pas annoncé comme exécutable dans PicoDbg v0.3.0** : Boot ROM, configuration d'horloges, pads et exceptions M33 manquent encore. L'ELF `debugger` n'essaie donc pas de faire passer un boot matériel complet pour une séquence émulée.

## Prérequis

Sur Debian/Ubuntu, à installer une fois :

```sh
sudo apt-get update
sudo apt-get install cmake make gcc g++ gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi python3 git
```

`arm-none-eabi-gcc --version` doit fonctionner. Pour `debugger`, aucun SDK, accès réseau au moment de compiler, ni bibliothèque C cible n'est nécessaire. `board` nécessite le **Pico SDK >= 2.2.0** ; la base documentaire utilisée ici est **2.2.0** :

```sh
git clone --branch 2.2.0 --depth 1 https://github.com/raspberrypi/pico-sdk.git "$HOME/pico-sdk"
git -C "$HOME/pico-sdk" submodule update --init --recursive
export PICO_SDK_PATH="$HOME/pico-sdk"
```

Le SDK peut également récupérer/construire `picotool` pour produire l'UF2 : sa première configuration peut nécessiter le réseau et les outils hôtes du SDK. Aucun téléchargement automatique de SDK n'est effectué par ce projet. Les builds `board` et `debugger`, ainsi que les deux cartes, utilisent des répertoires distincts.

## Compiler pour PicoDbg

Depuis la racine de PicoDebugger :

```sh
make -C tests/firmware/boot_blink debugger BOARD=pico2
./build-gui/pico-debugger --chip rp2350 tests/firmware/boot_blink/build/debugger-pico2/boot_blink.elf
```

Pour Pico 1 :

```sh
make -C tests/firmware/boot_blink debugger BOARD=pico
./build-gui/pico-debugger --chip rp2040 tests/firmware/boot_blink/build/debugger-pico/boot_blink.elf
```

Équivalent CMake, depuis la racine :

```sh
cmake -S tests/firmware/boot_blink -B tests/firmware/boot_blink/build/debugger-pico2 \
  -DBOOT_BLINK_MODE=debugger -DPICO_BOARD=pico2 -DBOOT_BLINK_HALF_PERIOD_MS=500
cmake --build tests/firmware/boot_blink/build/debugger-pico2 --parallel 2
```

Le build conserve les symboles, désactive LTO et utilise `-O0 -g3`. Les deux variantes hors ligne sont compilées en **Thumb Cortex-M0+** : le M33 réel sait exécuter ce sous-ensemble et le moteur actuel ne couvre pas le Thumb-2 général. Le build `board` Pico 2 utilise, lui, la cible ARM Cortex-M33 choisie par le SDK.

## Suivre le boot et la LED dans la GUI

1. Charger l'ELF `debugger` avec le profil correspondant. Il est arrêté sur `Reset_Handler`, avec SP au sommet de la SRAM et VTOR à `0x10000200`.
2. Ajouter les expressions **`read32(data_cookie)`**, **`read32(bss_cookie)`**, **`read32(boot_stage)`**, **`read32(boot_error)`**, **`read32(led_state)`**, **`read32(led_transitions)`** et **`read32(blink_count)`**.
3. Faire F10 pour entrer dans le startup, ou poser F9 sur **`main`** puis F5. À l'entrée de `main`, `data_cookie` vaut `0x5049434f`, les variables `.bss` valent zéro et `boot_stage` vaut encore 0. Avant le startup, `data_cookie` en SRAM valait zéro : le chargeur n'a pas effectué sa copie à la place du firmware.
4. Retirer ce breakpoint et poser F9 sur **`blink_checkpoint`**. F5 s'arrête après chaque changement de LED, lorsque les variables et les registres SIO sont cohérents.
5. Ouvrir **Périphériques** (`Ctrl+P`), inspecter **SIO** et filtrer **GPIO_OUT**. Pour une seconde vue, utiliser **⋯ → Dupliquer l’inspection** et filtrer **GPIO_OE**. Le bit 25 de GPIO_OUT alterne ; celui de GPIO_OE reste à 1. **Détacher** permet de placer ces vues à côté de l’assembleur ; **Rassembler** les remet en onglets.
6. Ouvrir **TIMER0** sur Pico 2, ou **TIMER** sur Pico 1, puis sélectionner **TIMERAWL**. Le compteur avance pendant F5/F10 et reste figé en pause.
7. Retirer le breakpoint pour une exécution continue ; **Maj+F5** arrête, **Ctrl+R** recommence le startup et remet les compteurs à zéro.

| Passage à `blink_checkpoint` | `led_state` | `led_transitions` | `blink_count` | GPIO_OUT bit 25 |
|---|---|---|---|---|
| 1 | 1 | 1 | 0 | 1 |
| 2 | 0 | 2 | 1 | 0 |
| 3 | 1 | 3 | 1 | 1 |
| 4 | 0 | 4 | 2 | 0 |

Le voyant **LED intégrée** de la barre d’état suit GP25. Le temps est virtuel et la durée affichée par le timer peut différer de la durée observée sur l’ordinateur. Pour parcourir rapidement les transitions avec le point d’arrêt, reconstruire avec `HALF_PERIOD_MS=1`.

Pour voir qui écrit le latch, un watchpoint doit viser **l'adresse SET ou CLR effectivement écrite**, pas seulement GPIO_OUT :

| Registre | Pico | Pico 2 |
|---|---|---|
| GPIO_OUT, lecture | `0xd0000010` | `0xd0000010` |
| GPIO_OUT_SET, écriture ON | `0xd0000014` | `0xd0000018` |
| GPIO_OUT_CLR, écriture OFF | `0xd0000018` | `0xd0000020` |
| GPIO_OE, lecture | `0xd0000020` | `0xd0000030` |
| GPIO_OE_SET, activation sortie | `0xd0000024` | `0xd0000038` |
| TIMERAWL | `0x40054028` | `0x400b0028` |

Un watchpoint de quatre octets en écriture s'arrête après le store. À cet instant, `led_state` peut encore avoir son ancienne valeur : l'application le met à jour juste après le registre. Le breakpoint `blink_checkpoint` évite cette ambiguïté.

## Compiler et démarrer sur la carte

```sh
# Depuis la racine de PicoDebugger, avec PICO_SDK_PATH exporté.
make -C tests/firmware/boot_blink board BOARD=pico2
# Ou : make -C tests/firmware/boot_blink board BOARD=pico
```

Équivalent CMake pour Pico 2 :

```sh
cmake -S tests/firmware/boot_blink -B tests/firmware/boot_blink/build/board-pico2 \
  -DBOOT_BLINK_MODE=board -DPICO_BOARD=pico2 -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build tests/firmware/boot_blink/build/board-pico2 --parallel 2
```

Maintenir **BOOTSEL** pendant le branchement USB, puis copier **`build/board-pico2/boot_blink.uf2`** sur le volume **RP2350**. Pour Pico, prendre **`build/board-pico/boot_blink.uf2`** et le volume **RPI-RP2**. Les chemins `build/...` sont relatifs au dossier de cet exemple. La copie remplace le firmware présent ; la carte redémarre et la LED doit alterner allumée/éteinte. Aucun port série ni bouton applicatif n'est nécessaire. Utiliser l'UF2 de la bonne carte.

Séquence effective sur carte :

1. La Boot ROM de la puce choisit et charge l'image Flash. Sur RP2040, le boot2 fourni par le SDK configure XIP ; sur RP2350, le SDK fournit les métadonnées de l'image ARM attendues par la ROM et le chemin de démarrage adapté.
2. Le startup du SDK établit l'environnement C, copie les données initialisées, efface `.bss` et initialise son runtime, notamment les horloges.
3. `main` vérifie ses témoins `.data`/`.bss`, initialise GPIO25 via `gpio_init`, place la sortie à zéro puis l'active.
4. La boucle écrit ON/OFF, attend avec `sleep_ms`, et recommence.

La configuration hors ligne commence directement au `Reset_Handler` de `startup/startup.c`. Elle effectue seulement les étapes C et les accès SIO/timer couverts, sans exécuter les étapes ROM/Flash/horloges du SDK.

## Variables et organisation

`boot_stage` : **0** avant `main`, **1** runtime C vérifié, **2** LED initialisée, **3** clignotement, **255** erreur. `boot_error` : **0** normal, **1** données initialisées incorrectes, **2** `.bss` incorrecte, **3** handler par défaut du startup hors ligne. Le programme reste en boucle sur erreur ; inspecter les variables puis réinitialiser. Le handler ne complète pas la gestion des exceptions M33 de l'émulateur.

- `src/main.c` : logique C commune, témoins de boot, point d'observation et compteurs.
- `src/platform_sdk.c` : GPIO/pads et temporisation réels, via les API C du SDK.
- `src/platform_debugger.c` : latches SIO et polling du timer virtuel, adresses distinctes par puce.
- `startup/startup.c`, `startup/memory_init.c` : vecteurs et startup C hors ligne commentés.
- `startup/debugger.ld.in` : placement Flash/SRAM, LMA de `.data`, `.bss`, pile de 4 Kio et contrôle de débordement au lien. Aucun tas ni constructeur C++.
- `CMakeLists.txt`, `Makefile` : builds autonomes ; aucune modification du moteur ou de la GUI requise.

## Vérifications

Tests hôtes de la copie `.data`, de l'effacement `.bss` avec sentinelles, et de trois cycles de la boucle C avec GPIO/attente substitués :

```sh
make -C tests/firmware/boot_blink host-test
```

Après installation du compilateur ARM et compilation des tests PicoDbg (depuis la racine du dépôt) :

```sh
cmake -S . -B build-gui -DPICODBG_GUI=ON -DBUILD_TESTING=ON
cmake --build build-gui --target picodbg_elf_tests --parallel 2
make -C tests/firmware/boot_blink check BOARD=pico
make -C tests/firmware/boot_blink check BOARD=pico2
```

`check` compile une image C distincte à demi-période de 1 ms. Il vérifie le format ELF et la LMA Flash de `.data`, l'arrivée dans `main`, les valeurs d'initialisation, VTOR, quatre transitions GPIO, les compteurs et un reset. Il échoue si un budget est épuisé avant le breakpoint attendu ou si une instruction n'est pas couverte. Le scénario appelle directement l'API du moteur depuis l'exécutable de tests `picodbg_elf_tests`, sans interface de commandes. On peut fournir `PICODBG_ELF_TESTS=/chemin/picodbg_elf_tests` pour un autre dossier de build, notamment un build moteur avec `PICODBG_GUI=OFF`.

**Validation lors de l'ajout :** les deux tests C sur l'hôte passent, ainsi que le contrôle de syntaxe C et les diagnostics de configuration. Le compilateur ARM et le SDK étaient absents de l'environnement, dont le terminal ne pouvait pas résoudre les adresses de téléchargement : **la compilation ARM, le parcours de cet ELF dans PicoDbg et la production UF2 n'ont pas pu être exécutés**. Ils ne sont pas remplacés par les ELF générés en Python du dépôt. Aucune validation sur carte n'a été effectuée.

**Validation après retrait de la CLI, le 20 novembre 2026 :** les deux tests hôtes et `make check` pour Pico et Pico 2 réussissent avec `arm-none-eabi-gcc` et le nouvel exécutable de tests lié au moteur. Cela valide les ELF de startup C dans le simulateur ; la production UF2 et l'exécution sur carte restent non vérifiées.

## Références officielles

- [Brochage LED, exemple C et chargement BOOTSEL](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html).
- [SDK 2.2.0 : installation et projet CMake](https://github.com/raspberrypi/pico-sdk/tree/2.2.0).
- [Startup ARM du SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2_common/pico_crt0/crt0.S).
- [SIO RP2040](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2040/hardware_regs/include/hardware/regs/sio.h) et [SIO RP2350](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/sio.h).
