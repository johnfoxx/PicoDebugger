# Déboguer une carte avec la Raspberry Pi Debug Probe

PicoDebugger utilise **GDB/MI → OpenOCD → Debug Probe → SWD** pour accéder à une vraie carte. Les vues CPU, assembleur, mémoire, pile, expressions et périphériques sont communes aux deux modes. En mode réel, aucune instruction ni aucun périphérique n'est émulé.

Cette intégration couvre les profils Pico/Pico W en RP2040 et Pico 2/Pico 2 W en RP2350 **ARM**. Les tests automatisés vérifient le protocole et l'interface avec des processus de test. **Les essais avec une sonde et les quatre cartes physiques restent à réaliser.**

## Préparer la connexion

1. Brancher la Debug Probe au PC par USB.
2. Relier sa sortie **D / SWD** à **SWDIO, SWCLK et GND** de la carte. Respecter les repères des connecteurs, pas une couleur de câble supposée.
3. Alimenter la carte, par exemple par son port USB. La liaison SWD à trois fils ne fournit pas son alimentation.
4. Installer un GDB compatible ARM, par exemple `gdb-multiarch` ou `arm-none-eabi-gdb`, et OpenOCD avec le support de la puce choisie. Un GDB limité à x86 ne convient pas. Sous Debian/Ubuntu, les paquets `gdb-multiarch` et `openocd` peuvent servir de point de départ ; vérifier le support RP2350 de la version distribuée.
5. Sur Linux, installer les règles udev de la distribution OpenOCD pour l'accès USB à CMSIS-DAP, puis rebrancher la sonde. Lancer le GUI avec l'utilisateur habituel.

La documentation Raspberry Pi décrit le [câblage, l'installation et le débogage avec la Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html). La sonde utilise SWD sur ces cartes. L'USB BOOTSEL/UF2 de la carte sert au chargement ; il ne fournit pas à lui seul cette interface de débogage.

Dans **Cible → Connecter la sonde…**, renseigner les chemins des exécutables si ceux-ci ne sont pas dans `PATH`. Le champ de scripts est le répertoire contenant `interface/` et `target/`, pas un fichier `.cfg`. Les chemins contenant des espaces sont acceptés. Les réglages sont conservés avec QSettings.

### OpenOCD lancé par l'application

Le réglage par défaut sélectionne CMSIS-DAP, 1 000 kHz et le port local 3333. PicoDebugger choisit `target/rp2040.cfg` ou `target/rp2350.cfg` selon la carte sélectionnée. Les serveurs Tcl et Telnet sont désactivés, GDB écoute sur l'interface locale. L'application ne termine que les processus qu'elle a lancés.

Il faut une version d'OpenOCD **avec ses scripts correspondants**. Copier seulement `rp2350.cfg` dans une ancienne installation ne lui ajoute pas les pilotes nécessaires. Les configurations Raspberry Pi [RP2040](https://github.com/raspberrypi/openocd/blob/sdk-2.3.0/tcl/target/rp2040.cfg) et [RP2350](https://github.com/raspberrypi/openocd/blob/sdk-2.3.0/tcl/target/rp2350.cfg) exposent les deux cœurs ARM par `hwthread`/SMP par défaut.

### Serveur externe

Décocher **Démarrer OpenOCD** pour se connecter à un serveur déjà lancé. Par exemple, pour RP2040 :

```sh
openocd -c "bindto 127.0.0.1" -c "tcl_port disabled" -c "telnet_port disabled" \
  -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 1000"
```

Pour RP2350 ARM, remplacer le fichier cible par `target/rp2350.cfg`. Indiquer ensuite l'hôte et le port GDB dans PicoDebugger. Le serveur externe reste ouvert après déconnexion.

La correspondance cœur 0/cœur 1 est déduite des noms OpenOCD `core0/core1`, `cm0/cm1` ou de l'attribut de cœur fourni par GDB. Si un serveur ne les fournit pas, renseigner les identifiants de threads physiques dans les champs avancés. Il ne faut pas les confondre avec des tâches RTOS. Un cœur absent est affiché indisponible ; une correspondance ambiguë est refusée. La connexion ne gère pas les tâches RTOS comme des cœurs physiques.

## Ouvrir, téléverser, lancer

L'application démarre en émulation. Choisir la carte avant de connecter la sonde ; sa sélection est verrouillée pendant la connexion. **La connexion arrête la cible sans téléversement automatique.** PicoDebugger lit `SYSINFO.CHIP_ID` et refuse une famille RP2040/RP2350 incompatible avant le téléversement. Ce contrôle ne distingue ni Pico de Pico W, ni Pico 2 de Pico 2 W : le profil W reste explicite.

En mode réel, **Ouvrir ELF** charge le fichier de symboles et ses métadonnées locales, sans programmer la carte. Un fichier ouvert en émulation est repris comme fichier de symboles à la connexion. Les symboles doivent correspondre au firmware présent sur la carte ; charger un fichier de symboles ne vérifie pas cette correspondance.

**Cible → Téléverser l'ELF sur la carte…** lance une action distincte, avec le nom de l'ELF et le mode de démarrage :

| Mode | Séquence | ELF attendu |
|---|---|---|
| Flash | Reset et arrêt, chargement GDB, nouveau reset et arrêt | Firmware ARM amorçable pour la carte sélectionnée, par exemple un ELF Pico SDK avec son démarrage matériel. Les segments doivent rester dans la Flash/SRAM du profil. |
| RAM | Reset et arrêt, chargement GDB, définition SP/PC/xPSR et VTOR du cœur 0, sans reset après transfert | ELF entièrement en SRAM, vecteurs reconnus (`.vectors`, `.isr_vector`, `.vector_table`, `__vectors` ou `__VECTOR_TABLE`), table alignée sur 256 octets, pile en SRAM et entrée Thumb en SRAM. |

Le mode RAM est un démarrage direct. Le firmware doit initialiser ses données/BSS, horloges et périphériques selon ses besoins ; il ne reproduit pas un boot Flash ou sécurisé. Le cœur 1 reste sous le contrôle du firmware et du serveur. Un ELF RAM sans table de vecteurs est refusé. Les ELF de démonstration générés dans `examples/` sont conçus pour le simulateur et ne constituent pas des firmwares Flash amorçables.

Le transfert écrit la mémoire de la carte, éventuellement sa Flash. Les changements du fichier depuis son ouverture imposent une nouvelle ouverture avant transfert. Un échec de transfert bloque **F5/F10** jusqu'à la réussite d'un nouveau téléversement dans cette connexion. Le programme n'est jamais lancé automatiquement après chargement : **F5** reprend son exécution.

| Action | Mode réel |
|---|---|
| F5 | Reprendre les cœurs exposés par le serveur. |
| F10 | Exécuter une instruction dans le contexte du cœur sélectionné ; le comportement des autres cœurs dépend du mode SMP et du serveur. |
| Maj+F5 | Demander l'arrêt et attendre sa notification. |
| Ctrl+R | Reset matériel puis arrêt ; ne remet pas arbitrairement toute la SRAM à zéro et ne recharge pas l'ELF. Pour une image RAM, retéléverser avec le mode RAM avant de la relancer. |
| F9 / double-clic assembleur | Ajouter ou retirer un breakpoint matériel d'adresse. |
| Watchpoint mémoire | Surveillance matérielle lecture, écriture ou accès, sur 1, 2 ou 4 octets alignés. |
| Ctrl+E / console `print` | Même langage d'expressions entières qu'en émulation ; lectures de la mémoire réelle à l'arrêt. |
| Démarrer un cœur explicitement | Modifier PC/SP/xPSR d'un cœur arrêté et exposé ; ne remplace pas le protocole de lancement multicœur du firmware. |

Les limites matérielles des breakpoints/watchpoints sont renvoyées par GDB/OpenOCD. Aucun watchpoint logiciel de remplacement n'est accepté. Avec GDB, un watchpoint en écriture s'arrête lorsque l'écriture change la valeur surveillée. Pour observer aussi une réécriture à l'identique, utiliser le mode accès (lecture ou écriture). Certains serveurs différant l'insertion jusqu'à la reprise peuvent signaler le manque de ressources au moment de F5. Les points affichés sont les définitions acceptées par GDB ; une erreur de reprise reste visible. [Watchpoints dans le manuel GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Set-Watchpoints.html)

## Lire l'état matériel

Les registres CPU de chaque cœur sont lus par GDB ; les registres supplémentaires exposés, par exemple FPU ou sécurité, sont ajoutés au tableau. Une valeur non exposée reste indisponible. Le désassemblage vient de GDB, ce qui évite de limiter la carte au décodeur Thumb partiel du simulateur. Mémoire, pile et expressions utilisent les lectures de la cible, jamais les octets d'un modèle émulé.

Les valeurs sont masquées pendant l'exécution et renouvelées après un arrêt. À l'arrêt, les lectures sont conservées pour que le tableau, les bits et les expressions partagent les mêmes valeurs. **Cible → Relire l'état matériel** invalide ce cache et relit les registres CPU ainsi que les plages demandées par les vues. Ce n'est pas une trace continue : les périphériques, DMA et watchdog ne sont pas nécessairement figés avec les CPU, et les deux cœurs/périphériques ne forment pas un instantané atomique.

Le catalogue décrit les mêmes registres et champs qu'en émulation, y compris ceux de blocs sans modèle logiciel. En mode réel, leurs valeurs peuvent être lues si le matériel le permet. Il reste **incomplet** : absence de description ne signifie ni absence du bloc matériel, ni émulation de sa valeur.

Seuls les registres dont la lecture automatique a été retenue sont interrogés par un rafraîchissement. Les autres affichent **Lecture explicite**. Sélectionner le registre puis **Lire une fois · effets possibles** pour une lecture réelle unique, dont la valeur est réutilisée pour les bits. Cette action peut consommer une donnée UART/SPI, acquérir un spinlock ou acquitter un indicateur. Un nouvel arrêt/rafraîchissement invalide cette lecture sans la répéter automatiquement. Les registres en écriture seule ne sont pas lus. Dans **Mémoire**, le bouton **Lire registre 32 bits une fois** permet également une lecture explicite d'une adresse MMIO alignée absente du catalogue. Le risque d'effet de lecture est le même ; les trois autres octets du mot sont réutilisés dans la vue hexadécimale sans nouveaux accès matériels.

La lecture d'une zone protégée ou inaccessible affiche l'erreur du serveur ; aucune valeur de reset ou zéro fictif ne la remplace. Les tables restent en lecture seule : pas d'éditeur arbitraire de mémoire ou de registres, ni d'appels de fonctions cibles dans les expressions.

## Limites qui restent présentes

- ARM/Thumb uniquement, pas de chargement ELF Hazard3/RISC-V, UF2 ou BIN.
- Désassemblage et symboles ELF ; pas de pas source C/C++, variables DWARF ni déroulage de pile.
- Pas de contournement des permissions de debug, de TrustZone, des protections OTP ou d'une configuration verrouillée. Les registres accessibles dépendent de la cible et du serveur.
- Le CYW43 des cartes W est un composant externe piloté par le firmware ; ses registres internes et son état radio ne sont pas exposés directement par le SWD du RP2040/RP2350.
- Le temps virtuel et l'injection UART simulée sont désactivés. Le port **U / UART** de la Debug Probe nécessite son câblage série et un terminal externe ; cette liaison n'est pas encore intégrée à la GUI.
- Le succès en émulation ne certifie pas le comportement matériel. Un firmware qui utilise des blocs non émulés peut fonctionner sur carte tout en arrêtant le simulateur.

## Déconnexion et diagnostic

La barre d'état indique **ÉMULATION** ou **CARTE RÉELLE**, la carte choisie et la connexion : en cours, arrêtée, en exécution, déconnectée ou perdue. Survoler le texte pour voir le détail complet. Les messages GDB/OpenOCD sont conservés dans la console.

Une connexion en cours peut être annulée. Une opération de transfert doit se terminer avant une déconnexion normale. Déconnecter une cible en exécution demande d'abord son arrêt, retire les points créés par PicoDebugger puis ferme la connexion. Aucune reprise automatique n'est demandée. En cas de perte de liaison, l'état final de la carte n'est plus confirmé et les données affichées sont invalidées. Passer en émulation recharge le fichier ELF local ; les points de la session matérielle ne sont pas transférés au simulateur.

| Symptôme | Vérification |
|---|---|
| Exécutable introuvable | Chemin GDB/OpenOCD ; utiliser un GDB avec support ARM. |
| Sonde introuvable / accès USB refusé | Câblage USB, permissions udev, sonde occupée par un autre logiciel. |
| Pas de réponse SWD | Alimentation de la carte, masse commune, SWDIO/SWCLK ; essayer une vitesse plus basse. |
| `rp2350.cfg` absent / pilote inconnu | Installer une version OpenOCD RP2350 et ses scripts correspondants. |
| Famille incompatible | Corriger le profil de carte avant de reconnecter. La variante W n'est pas autodétectée. |
| Cœurs non identifiés | Configuration OpenOCD `hwthread`/SMP ou correspondance explicite des threads physiques. |
| Plus de points d'arrêt | Retirer des points ou des watchpoints ; capacité imposée par la cible. |
| Valeur indisponible | CPU en exécution, cœur absent, registre non exposé/protégé, ou lecture explicite requise ; consulter l'infobulle et la console. |
| Échec de Flash / téléversement interrompu | Examiner les diagnostics, corriger le firmware/la connexion puis refaire le transfert ; la reprise d'une image partielle est bloquée. |

Pour valider sur matériel : connecter chaque famille, charger un ELF Pico SDK connu, placer un breakpoint dans `main`, lancer, vérifier un pas et un watchpoint SRAM, contrôler les deux contextes CPU, inspecter un registre, débrancher/rebrancher la sonde puis revenir en émulation. Tester aussi un ELF lié en RAM avec vecteurs, et la variante W avec son firmware adapté. Ces essais ne sont pas remplacés par les tests automatiques.
