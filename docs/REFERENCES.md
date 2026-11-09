# Références et provenance

Références consultées le 8 novembre 2026. Le SDK **2.2.0** est une référence figée pour les adresses et descriptions de registres ; ce n'est pas une affirmation qu'il s'agit du dernier SDK. Les liens `master` ne doivent pas servir de référence de conformité.

## Documents officiels

| Document | Usage dans ce projet | État de consultation |
|---|---|---|
| [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf) | §2.1 bus/alias, §2.2 cartographie, §2.3 SIO, §2.4 CPU, §4.6 timer | PDF consulté en ligne ; build `2025-02-20`, `3184e62-clean`, 642 pages |
| [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf) et [portail des révisions](https://pip.raspberrypi.com/categories/1214-rp2350) | Architecture du RP2350, différences et errata | PDF complet non récupéré dans cette session ; adresses et timer vérifiés dans les en-têtes officiels ci-dessous |
| [ARMv6-M Architecture Reference Manual, DDI0419C](https://support.arm.com/documentation/ddi0419/c/?lang=en) | Sémantique des instructions, exceptions et règles architecturales du M0+ | Référence de conformité à utiliser pour la validation instruction par instruction |
| [Cortex-M33 Generic User Guide](https://support.arm.com/documentation/100235/latest/) | PPB, NVIC, exceptions et options du M33 | Référence identifiée ; le M33 complet reste à implémenter |
| [Boot ROM RP2040](https://github.com/raspberrypi/pico-bootrom-rp2040) / [RP2350](https://github.com/raspberrypi/pico-bootrom-rp2350) | Futurs boot, API ROM et protocole de lancement du cœur 1 | Dépôts identifiés, aucun code ROM intégré ni exécuté |

Les liens de datasheets peuvent servir une révision plus récente à la même adresse. Pour reproduire une validation, conserver la date, la révision indiquée dans le document et l'empreinte du fichier réellement utilisé.

## En-têtes officiels utilisés

| Cible | Cartographie | Timer |
|---|---|---|
| RP2040 | [addressmap.h, SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2040/hardware_regs/include/hardware/regs/addressmap.h) | [timer.h, SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2040/hardware_regs/include/hardware/regs/timer.h) |
| RP2350 | [addressmap.h, SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/addressmap.h) | [timer.h, SDK 2.2.0](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/timer.h) |

Ces en-têtes portent une licence BSD-3-Clause. Les métadonnées du projet décrivent les registres utilisés ; elles ne constituent pas un import exhaustif du SDK ou d'un SVD. Le catalogue décrit les blocs matériels même quand leurs registres n'ont pas encore été ajoutés.

Extension Pico 2, consultation du 8–9 novembre 2026 : [UART](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/uart.h), [SPI](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/spi.h), [PWM](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/pwm.h), [SIO](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/sio.h), [IRQ](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/intctrl.h), [RESETS RP2350](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/resets.h), [RESETS RP2040](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2040/hardware_regs/include/hardware/regs/resets.h), [WATCHDOG scratch](https://github.com/raspberrypi/pico-sdk/blob/2.2.0/src/rp2350/hardware_regs/include/hardware/regs/watchdog.h). Les IRQ série ne sont pas aux mêmes indices sur les deux puces ; le RP2350 nécessite deux banques NVIC.

Le [manuel ARM PL011 DDI0183G, §2.8](https://documentation-service.arm.com/static/5e8e36c2fd977155116a90b5) précise les transitions de seuil des FIFO et l'acquittement des interruptions : un UART initialement vide ne doit pas produire spontanément une IRQ TX. Ce comportement fait l'objet d'un test dédié. Les sources Qt5 expliquant le message Wayland sont référencées dans [BOOT_AND_PERIPHERALS.md](BOOT_AND_PERIPHERALS.md).

## Choix issus de cette lecture

- Le timer RP2040 possède un compteur partagé de 64 bits, une lecture haute mémorisée et quatre comparateurs sur la partie basse. Son tick matériel dépend du watchdog et des horloges (§4.6). Le modèle remplace explicitement cette source par des quanta virtuels.
- Les alias d'écriture RP2040 utilisent les décalages `0x1000` XOR, `0x2000` SET, `0x3000` CLR (§2.1.2). Ici seuls les registres RW du timer sont couverts ; les alias de registres à effets de bord sont refusés.
- Les écritures MMIO étroites ont des règles propres au bus (§2.1.4). Elles restent hors couverture : un accès octet/demi-mot ne doit pas être transformé arbitrairement en mise à jour partielle d'un registre.
- Les timers RP2350 ajoutent `LOCKED` et `SOURCE` et déplacent les registres IRQ. Le modèle sélectionne ces particularités explicitement ; les sources de temps restent virtuelles, sans simulation du bloc TICKS/clk_sys.

## Récupération locale

La consultation en ligne est distincte de la présence de fichiers dans le dépôt. Le script suivant récupère les deux PDF et les en-têtes référencés pour les deux puces dans un cache ignoré par Git :

```sh
python3 scripts/fetch-reference-docs.py --list
python3 scripts/fetch-reference-docs.py
# Ou une sélection :
python3 scripts/fetch-reference-docs.py rp2040-datasheet rp2040-timer
```

Il vérifie le format, borne la taille et écrit les fichiers seulement après un téléchargement réussi. `docs/reference-cache/manifest.json` conserve URL, date UTC, taille et SHA-256 ; `--refresh` permet de récupérer une nouvelle révision. Cette empreinte identifie le contenu téléchargé, sans remplacer une signature de l'éditeur. Les documents restent soumis à leurs licences respectives.

Dans l'environnement de développement de cette session, le terminal n'a pas pu résoudre les hôtes externes. **Aucun PDF local n'est annoncé comme téléchargé.** Le script est fourni pour une machine ayant accès au réseau ; le débogueur fonctionne indépendamment de ce cache.

## Trajectoire de conformité

L'[état actuel](COVERAGE.md) est la référence sur les capacités livrées. Pour poursuivre vers une exécution fidèle :

1. Fixer les profils de silicium/ROM/Flash et la version du SDK de chaque firmware de référence. Les [révisions RP2350 A4](https://www.raspberrypi.com/news/rp2350-a4-rp2354-and-a-new-hacking-challenge/) et leurs errata nécessitent un profil explicite ; le nom « Pico 2 » seul ne suffit pas à une validation de conformité.
2. Compléter et comparer instructions, flags, exceptions, NVIC et fautes contre une référence indépendante. Tester limites, accès interdits, empilement et retour, pas seulement les programmes nominaux.
3. Modéliser resets, horloges/watchdog et planification des événements, puis intégrer une ROM identifiée pour le boot et le lancement multicœur. Remplacer les raccourcis de démarrage actuels.
4. Ajouter chaque périphérique avec son modèle d'état séparé : SIO/diviseur/interpolateurs, GPIO/pads, UART/SPI/I2C, PWM/ADC, DMA/DREQ, PIO et USB/XIP. L'import de descriptions de registres facilitera l'inspection, sans remplacer ces comportements.
5. Compléter ARMv8-M/Thumb-2, FPU, TrustZone, MPU et les périphériques propres au RP2350. La présence d'une adresse RP2350 dans la GUI n'est pas une preuve d'exécution Cortex-M33.
6. Exécuter un corpus de firmwares C++ avec différentes optimisations et des scénarios d'entrées enregistrés. Comparer sorties, accès MMIO et IRQ au matériel ; traiter séparément la conformité fonctionnelle et temporelle.

Chaque extension doit documenter sa source, ses hypothèses, les tests effectués et ce qui reste non validé. Une validation sur carte n'est pas nécessaire pour utiliser le débogueur hors ligne, mais elle est nécessaire pour étayer ses promesses de fidélité.
