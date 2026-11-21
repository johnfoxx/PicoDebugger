# Tutoriel PicoDebugger — interface courante

Pour le **mode réel SWD**, voir [connexion et usage de la Debug Probe](HARDWARE_DEBUG.md). Les descriptions de simulation, temps virtuel et accès sans effet de bord de ce document concernent le **mode émulation**.

Ce guide décrit les fonctions disponibles et l'interface réorganisée après v0.3.0. Voir aussi [les repères de navigation](INTERFACE.md). Toutes les commandes de terminal se lancent depuis la racine du dépôt, après [compilation de la GUI](../README.md#installation-et-lancement). Les exemples intégrés ne nécessitent ni carte, ni sonde, ni compilateur ARM.

## Sommaire

1. [Première session et commandes de la GUI](#1-première-session-et-commandes-de-la-gui)
2. [Pas, registres et assembleur](#2-pas-registres-et-assembleur)
3. [Breakpoints et watchpoints](#3-breakpoints-et-watchpoints)
4. [Expressions surveillées et console](#4-expressions-surveillées-et-console)
5. [Mémoire, pile et cartographie](#5-mémoire-pile-et-cartographie)
6. [Deux cœurs](#6-deux-cœurs)
7. [Périphériques et fenêtres indépendantes](#7-périphériques-et-fenêtres-indépendantes)
8. [Exemple Pico 2 : UART, timer, PWM et GPIO](#8-exemple-pico-2--uart-timer-pwm-et-gpio)
9. [Charger son ELF et gérer la session](#9-charger-son-elf-et-gérer-la-session)
10. [Erreurs, limites et dépannage Qt](#10-erreurs-limites-et-dépannage-qt)
11. [Cas d'usage](#11-cas-dusage)

Pour les ajouts après v0.4.0 : [fenêtre de périphériques et navigation](INTERFACE.md), [temps virtuel](VIRTUAL_TIME.md), [cartes W et compatibilité ELF](BOARDS.md).

## 1. Première session et commandes de la GUI

```sh
./build-gui/pico-debugger --demo
```

La fenêtre s'ouvre maximisée. Garder **Pico / RP2040** et **Contexte : cœur 0** pour les chapitres 1 à 6. Le cœur 0 est arrêté avant la première instruction ; le cœur 1 est **RETENU**. La sélection d'un cœur ne le démarre pas.

Les quatre cartes du sélecteur utilisent deux modèles de puces partiels : celui du RP2040 possède un modèle d'exceptions plus avancé, celui du RP2350 refuse encore les entrées d'exception M33. L'aide détaille les autres différences. Ce libellé porte sur la simulation du logiciel, pas sur la fiabilité de la carte physique.

| Action | Accès |
|---|---|
| Charger un ELF | **Fichier → Ouvrir un ELF…**, barre d'outils, `Ctrl+O` sous Linux |
| Recharger l'exemple intégré | **Fichier → Exemple intégré**, barre d'outils |
| Choisir la carte / le contexte cœur | Les deux listes de la barre d'outils |
| Pas instruction du cœur sélectionné | **Débogage → Pas instruction**, `F10` |
| Exécuter tous les cœurs démarrés | **Débogage → Exécuter les cœurs**, `F5` |
| Pause | **Débogage → Pause**, `Maj+F5` |
| Reset/restart du modèle | **Débogage → Reset / restart**, `Ctrl+R` |
| Démarrer ou réinitialiser un cœur | **Débogage → Démarrer un cœur explicitement…** |
| Ajouter un breakpoint | **Débogage → Ajouter un point d'arrêt…**, `F9`, ou **+ Arrêt** |
| Ajouter un watchpoint mémoire | **Débogage → Ajouter un watchpoint mémoire…**, ou **+ Accès** |
| Afficher/masquer un panneau | Entrée correspondante du menu **Vues** |
| Restaurer la disposition initiale | **Vues → Rétablir la disposition initiale**, `Ctrl+Maj+0` |
| Ouvrir le catalogue périphériques | **Vues → Catalogue des périphériques**, `Ctrl+P` ou `Ctrl+2` |
| Ouvrir la fenêtre autonome des périphériques | **Vues → Fenêtre des périphériques…**, `Ctrl+Maj+I` |
| Avancer le temps virtuel | **Vues → Temps virtuel**, `Ctrl+6` |
| Voir les connexions de la carte | **Vues → Carte et câblage**, `Ctrl+7` |
| Rassembler les inspecteurs détachés | **Vues → Rassembler les inspecteurs**, `Ctrl+Maj+P` |
| Ajouter une expression | **Débogage → Ajouter une expression surveillée…**, `Ctrl+E` |
| Choisir Code / Mémoire / Pile / Console | `Ctrl+1` / `Ctrl+3` / `Ctrl+4` / `Ctrl+5` |
| Lire la couverture actuelle | **Aide → Couverture et limites** |
| Fermer l'application | **Fichier → Quitter** ou fermeture de la fenêtre principale |

Le tableau **CPU** à gauche compare les deux cœurs. Au centre, **Code** affiche l'assembleur et **Périphériques** le catalogue ; les inspecteurs ouverts y ajoutent leurs propres onglets. En bas, **Inspection** regroupe Mémoire, Pile, Console et Carte mémoire. La colonne **Surveillance**, masquée au départ, apparaît à l'ajout d'un point ou d'une expression ; le menu **Vues** permet également de l'ouvrir.

Faire glisser les séparations pour redimensionner les zones. Fermer un panneau le masque sans effacer ses données ; **Vues** ou la restauration de disposition le réaffiche. Cette dernière revient à Code/Mémoire et masque Surveillance, sans fermer les inspecteurs. La disposition n'est pas sauvegardée entre deux lancements.

Sous Qt5/Wayland, le détachement d'un dock par glisser-déposer est désactivé pour contourner une demande d'activation interne à Qt. Le bouton **Détacher** des inspecteurs reste disponible ; le placement et le focus des fenêtres dépendent du compositeur.

## 2. Pas, registres et assembleur

Avec l'exemple fraîchement chargé :

1. Appuyer trois fois sur **F10**.
2. Observer **R0 = `0x00000001`**, **R1 = `0x20000000`**, **PC = `0x10000206`** du cœur 0.
3. La flèche de l'assembleur désigne le prochain `STR` ; le compteur en mémoire vaut encore zéro.
4. Appuyer une quatrième fois sur **F10** : le premier mot à `0x20000000` devient `1`.

Les cellules de registres modifiées sont surlignées entre rafraîchissements. Les deux cœurs sont visibles simultanément dans les colonnes du même tableau ; le point dans l’en-tête marque le contexte sélectionné. SP/LR/PC correspondent à R13/R14/R15 ; les autres lignes montrent xPSR, MSP, PSP, CONTROL et PRIMASK. Les flags NZCV/T/IPSR sont affichés au-dessus du tableau. Ces vues ne permettent pas d'éditer les registres.

**F10** concerne seulement le cœur sélectionné, mais fait aussi avancer les périphériques et SysTick en temps virtuel. **F5** alterne les cœurs démarrés ; ce n'est pas une exécution parallèle avec timing matériel. La pause fige le modèle. Pendant F5, les tableaux sont des instantanés cohérents entre des tranches courtes : toutes les transitions intermédiaires ne sont pas affichées. Utiliser F10 pour observer chaque pas. L'horodatage de la barre d'état est celui de l'hôte ; la révision identifie l'état inspecté.

Dans l'assembleur :

- **Suivre PC** maintient la vue sur le PC du cœur sélectionné.
- Saisir `Reset_Handler`, `core1_entry`, `0x10000200` ou une expression d'adresse, puis **Afficher** ou Entrée, pour explorer une autre zone. Cette action désactive le suivi ; le réactiver avec la case.
- La vue montre jusqu'à 32 instructions à partir de l'adresse choisie, avec opcode et symbole ELF proche.
- La pastille `●` signale un breakpoint ; un double-clic sur une instruction l'ajoute ou le retire.

Pour un ELF créé avec `-g`, cliquer la colonne **Source C** affiche la ligne associée. L'IDE suit aussi cette correspondance dans les fichiers du projet. **F10** avance d'une instruction. [Correspondance C et assembleur](IDE.md#suivre-le-c-et-lassembleur)

## 3. Breakpoints et watchpoints

### S'arrêter avant une instruction

1. Faire **Ctrl+R**.
2. Appuyer sur **F9**, saisir `0x10000206`, valider.
3. Faire **F5**. L'arrêt arrive **avant** le `STR` : R0 vaut 1, `counter` vaut encore 0.
4. Faire **F10** : le pas manuel exécute l'instruction malgré le breakpoint courant ; `counter` devient 1.
5. Sélectionner ce point dans **Surveillance → Arrêts**, puis **Supprimer**.

Les breakpoints sont des adresses communes aux deux cœurs. Ils ne possèdent pas de condition, compteur d'occurrences ou restriction par cœur. Une reprise F5 peut franchir une fois le breakpoint qui vient d'arrêter l'exécution ; une visite ultérieure s'y arrête à nouveau.

Un `BKPT` dans le programme est aussi un arrêt de débogage. Contrairement au breakpoint d'adresse, il a été exécuté et PC avance. L'exemple atteint `BKPT #0` quand son compteur vaut 5 ; après ce `BKPT`, il contient une boucle, pas une commande de fermeture de l'application. Utiliser **Ctrl+R** pour recommencer.

### Trouver qui écrit une zone mémoire

1. Supprimer le breakpoint précédent et faire **Ctrl+R**.
2. Utiliser **Débogage → Ajouter un watchpoint mémoire…**, ou **+ Accès** dans **Surveillance → Arrêts**. Adresse : `counter`, taille : `4` octets, accès : **Écriture**.
3. Faire **F5** : arrêt **après** le premier `STR`. Le compteur vaut maintenant 1 et PC a avancé.
4. Faire F5 à nouveau pour atteindre l'écriture suivante, ou supprimer ce watchpoint pour atteindre le `BKPT` final.

Le dialogue accepte une plage de 1 à 65 536 octets et les modes **Écriture**, **Lecture**, **Lecture et écriture**. Un accès qui chevauche la plage suffit. L'arrêt est après l'instruction : un transfert multiple ou un empilement peut déjà avoir modifié plusieurs mots. Le message donne l'accès observé ; ce n'est pas un journal exhaustif de tous les accès de l'instruction.

Un watchpoint observe les accès CPU des deux cœurs, pas le fetch des instructions, les lectures de la GUI ou les changements internes d'un périphérique au passage du temps. Lire `counter` à l'écran ne déclenche donc pas le watchpoint. **Supprimer** retire uniquement le point sélectionné.

## 4. Expressions surveillées et console

Faire **Ctrl+E**, ou afficher **Vues → Expressions surveillées** puis cliquer **+ Expression** et saisir `read32(counter)`. La ligne est liée au cœur sélectionné au moment de sa création ; changer le sélecteur principal ne change pas son contexte. Pour changer ce contexte, supprimer la ligne et la recréer. Une expression invalide reste visible avec **Indisponible** et un diagnostic en infobulle.

La console graphique accepte une expression, éventuellement précédée de `print` ou `p`, puis Entrée :

```text
counter
print read32(counter)
p r0 + 4
read16(0x10000200)
*sp
(xpsr >> 28) & 0xf
```

La console utilise le cœur actuellement sélectionné et affiche le résultat en hexadécimal et en entier non signé. Elle accepte les expressions et la commande `print` ; les actions d'exécution, de reset, de chargement et de watchpoint passent par les boutons et menus correspondants. Le journal conserve au plus 2 000 blocs de texte.

| Syntaxe | Sens |
|---|---|
| `42`, `0x2a` | Entiers décimaux ou hexadécimaux dans l'intervalle 0…`0xffffffff` |
| `r0`…`r15`, `sp`, `lr`, `pc` | Registre CPU ; préfixe `$` également accepté, par exemple `$pc` |
| `xpsr`, `msp`, `psp`, `control`, `primask` | Registres système exposés par le modèle |
| `counter`, `Reset_Handler` | Adresse d'un symbole ELF, **pas** contenu ou variable typée |
| `read8(a)`, `read16(a)`, `read32(a)` | Lecture d'inspection de 1, 2 ou 4 octets |
| `*a` | Lecture 32 bits, équivalente à `read32(a)` |
| `+ - * / %`, `& ^ \|`, `<< >>` | Arithmétique et opérations binaires ; priorités usuelles, parenthèses acceptées |
| `+a`, `-a`, `~a` | Opérations unaires ; résultats sur 32 bits |

Les calculs sont non signés sur 32 bits, avec débordement modulo 2³². Les décalages doivent être entre 0 et 31 ; division par zéro refusée. Les comparaisons, booléens C++, casts, accès aux membres, affectations, nombres flottants et appels de fonctions cibles ne sont pas implémentés. La profondeur des expressions est bornée.

Les lectures d'inspection n'ont pas d'effet de bord : `read32` d'une FIFO ne la dépile pas. Pour les MMIO, seuls les mots 32 bits alignés couverts par le modèle sont inspectables. Un symbole C++ peut nécessiter son nom manglé exact : le démanglage et DWARF ne sont pas disponibles.

## 5. Mémoire, pile et cartographie

Dans **Inspection → Mémoire** (`Ctrl+3`), saisir `counter` ou `0x20000000`, choisir **16** octets, puis **Lire**. Les octets sont présentés dans l'ordre des adresses : le mot `1` apparaît `01 00 00 00`. La longueur se règle de 16 à 4 096 octets. Les octets non imprimables apparaissent comme des points en ASCII ; `??` indique une lecture indisponible, dont l'infobulle précise la cause. La vue est en lecture seule.

La vue **Inspection → Pile** (`Ctrl+4`) affiche jusqu'à 32 mots depuis son SP. La première ligne est marquée `SP ▶`. Les noms de régions aident à reconnaître une adresse en Flash/SRAM ; ils ne prouvent pas qu'un mot soit une adresse de retour. Il n'existe pas encore de déroulage de pile C++ ou de liste de variables locales. Un cœur retenu dont SP vaut zéro n'a pas de pile initialisée à inspecter.

La **Carte mémoire**, dans les onglets du bas, expose début, fin incluse, permissions et couverture de chaque région. Double-cliquer une région place son adresse de début dans la vue mémoire. Une plage cartographiée n'est pas nécessairement lisible intégralement : ROM absente, réserves et registres non implémentés restent indisponibles.

La vue hexadécimale utilise des lectures en octets : elle n'est pas adaptée aux MMIO. Pour un registre périphérique, utiliser son inspecteur ou une expression `read32(adresse)` alignée. Cela évite de confondre une absence d'accès octet avec une absence de valeur 32 bits disponible.

## 6. Deux cœurs

Recharger **Fichier → Exemple intégré** pour partir sans breakpoint ni watchpoint mémoire. Les symboles `counter`, `counter1` et `core1_entry` sont inclus.

1. Ouvrir **Débogage → Démarrer un cœur explicitement…**.
2. Choisir **Cœur 1**, entrée `core1_entry`, SP `0x20042000`, puis **Démarrer à l'arrêt**.
3. Choisir **Contexte : cœur 1** et faire F10 pour observer uniquement ses instructions.
4. Ajouter l'expression `read32(counter1)` dans ce contexte, puis revenir au cœur 0 et ajouter `read32(counter)`.
5. Faire F5 pour alterner les deux cœurs. Les compteurs visent respectivement 5 et 10. Le premier `BKPT` arrête l'exécution globale ; reprendre avec F5 si l'autre cœur n'a pas encore atteint son `BKPT`.

Le lancement explicite initialise les registres de ce cœur avec les adresses fournies ; il ne recharge pas la RAM partagée et peut aussi réinitialiser le cœur 0. Une entrée Thumb doit avoir son bit 0 à 1, par exemple `0x10000221`. SP doit être aligné sur 4 octets et appartenir à la SRAM du profil. Pour un firmware personnel, réserver des piles distinctes dans son plan mémoire. Dans ce dialogue, les expressions de registres pour l'entrée et SP sont évaluées avec le contexte cœur 0 ; les symboles/adresses explicites évitent une ambiguïté de contexte.

Le cœur 1 reste retenu après chaque chargement/reset. Ce mécanisme ne remplace pas le protocole ROM/FIFO du SDK. La SRAM et les périphériques partagés sont communs ; les registres CPU et système sont distincts. Le modèle permet l'étude de ces états, sans reproduire tous les entrelacements, arbitrages ou courses du matériel réel.

## 7. Périphériques et fenêtres indépendantes

Le catalogue est accessible même avant le chargement d'une image. Le premier filtre recherche un périphérique, registre, champ ou adresse ; sélectionner ensuite le bloc trouvé. Sa couverture et les infobulles distinguent les modèles partiels des blocs non émulés.

1. Faire **Ctrl+P**, puis double-cliquer le bloc ou utiliser **Inspecter**. Un onglet s'ouvre ; un inspecteur déjà ouvert est retrouvé au lieu d'être dupliqué.
2. Ouvrir d'autres inspecteurs : chacun conserve son périphérique, son filtre et son contexte cœur.
3. Sélectionner un registre puis **Bits** pour voir positions, valeurs et significations. **Registres** revient à la liste ; un double-clic sur un registre ouvre aussi ses bits.
4. Utiliser le filtre pour chercher, par exemple, `RXFE`, `CH15`, `IC_STATUS` ou une adresse. Une recherche de périphérique sans résultat affiche une indication et désactive **Inspecter**.
5. Pour comparer le périphérique à l'assembleur, cliquer **Détacher** puis revenir à **Code**. Déplacer/redimensionner la fenêtre indépendante, éventuellement sur un autre écran.
6. Pour une seconde vue du même périphérique, utiliser **⋯ → Dupliquer l'inspection** et choisir son contexte cœur.

Une infobulle sur un champ donne sa description et les valeurs symboliques connues. Les bits non décrits ne sont pas automatiquement réservés. Les significations décrivent le matériel ; certains modes nommés restent hors couverture d'exécution du modèle. Les changements de valeurs sont surlignés entre instantanés.

| Affichage d'une valeur | Signification |
|---|---|
| Hexadécimal | Valeur disponible dans le modèle, lue sans effet de bord |
| **Pas d'image** | Aucun programme chargé pour cette session |
| **Non émulé** | Registre décrit dans un bloc sans modèle d'exécution |
| **Écriture seule** | Registre WO décrit, sans lecture d'état proposée |
| **Indisponible** | Le modèle ne fournit pas cette lecture ; consulter l'infobulle |
| `—` dans le détail des bits | Aucune valeur brute disponible pour décoder ce champ |

Exemple de documentation sans simulation : dans le profil Pico 2, sélectionner **ADC**, filtrer `AINSEL`, puis sélectionner `CS`. Les bits du sélecteur analogique sont décrits, mais l'état reste **Non émulé** après chargement. Aucune tension, conversion ou valeur de reset n'est inventée.

**Rattacher**, ou fermer une fenêtre détachée, ramène le même inspecteur dans son onglet. **Rassembler** / `Ctrl+Maj+P` rattache toutes les fenêtres ; filtres, sélection, contexte et texte RX non envoyé sont conservés. La croix d'un onglet ferme cet inspecteur. `Ctrl+Maj+0` restaure la disposition principale sans fermer les inspecteurs. Changer de puce les ferme tous ; reset et nouveau fichier dans le même profil les actualisent.

## 8. Exemple Pico 2 : UART, timer, PWM et GPIO

```sh
python3 examples/make_peripheral_demo.py build-gui/rp2350-peripherals.elf --chip rp2350
./build-gui/pico-debugger --chip rp2350 build-gui/rp2350-peripherals.elf
```

Cet exemple possède ses propres vecteurs et instructions Thumb, sans dépendance au démarrage du SDK. Il configure les modèles accessibles en polling et évite de demander un handler d'IRQ M33.

1. Ouvrir les inspecteurs **UART0**, **TIMER0**, **PWM** et **SIO**. Les détacher seulement si une observation simultanée est utile.
2. Faire F10 pour observer l'initialisation, ou F5 pour atteindre **BKPT #3**.
3. Dans **UART0 → UART · TX / RX**, la sortie affiche **OK**. Le programme a activé PWM0, positionné les latches de sortie GPIO25, attendu l'alarme TIMER0 et écrit un compteur virtuel non nul à `0x20000000`.
4. Dans **UART0 → Registres**, filtrer `UARTLCR_H` puis utiliser **Bits** : `WLEN=3` se décode en **8 bits** et `FEN=1` active la FIFO du modèle.
5. Revenir à **UART · TX / RX**, saisir `RX` dans l'entrée UART, puis **Injecter RX** ou Entrée. La GUI met l'exécution en pause et indique le nombre d'octets acceptés.
6. Revenir à **Registres**, effacer le filtre, sélectionner `UARTDR` : le premier octet en attente vaut `0x52`, soit `R`. Les rafraîchissements ne le consomment pas. Un programme qui lirait réellement UARTDR le dépilerait.
7. Dans TIMER0, observer `TIMERAWL`, `ALARM0` et `INTR`. Dans PWM, filtrer `CH0`. Dans SIO, consulter GPIO_OUT/OE. Ce sont des états numériques du modèle, sans signal électrique externe.

UARTEN et RXE doivent être actifs et la FIFO doit avoir de la place. La saisie RX est encodée en UTF-8, sans ajout automatique de retour ligne. La sortie TX montre les derniers octets capturés (environ 64 Kio), affichés en Latin-1 : ce n'est pas un terminal UTF-8 complet ni une émulation de terminal série. Le reset efface cet historique. L'injection est indisponible après un arrêt terminal nécessitant un reset.

Les compteurs sont virtuels : à l'arrêt, ils n'avancent pas avec l'horloge de l'ordinateur. Un pas peut les faire avancer même si l'instruction n'accède pas au périphérique. Les IRQ RP2350 peuvent être visibles dans NVIC, mais leur prise provoque un arrêt hors couverture M33.

Pour le même exemple en RP2040 :

```sh
python3 examples/make_peripheral_demo.py build-gui/rp2040-peripherals.elf --chip rp2040
./build-gui/pico-debugger --chip rp2040 build-gui/rp2040-peripherals.elf
```

Le timer s'appelle alors **TIMER**, et les adresses série/PWM diffèrent. Ces images ne sont pas des firmwares UF2/SDK amorçables sur carte.

## 9. Charger son ELF et gérer la session

Choisir d'abord la puce, puis **Fichier → Ouvrir un ELF…**. Les extensions `.elf` et `.axf` sont proposées ; le contenu doit être ELF32 ARM little-endian, exécutable lié `ET_EXEC`, sans interpréteur dynamique. Choisir « Tous les fichiers » ne change pas ces contraintes.

Le chargeur place les segments `PT_LOAD` à leur adresse physique/LMA. Le startup du firmware reste responsable de la copie `.data` de Flash vers RAM. Les vecteurs sont reconnus via `.vectors`, `.isr_vector`, `.vector_table`, `__vectors` ou `__VECTOR_TABLE`. SP doit être valide et la table reconnue alignée sur 256 octets. À défaut de vecteurs reconnus, le moteur utilise `e_entry` et le sommet SRAM ; ce raccourci apparaît dans le diagnostic. L'entrée doit être Thumb, bit 0 à 1.

Après chargement, lire le diagnostic **PC/SP/VTOR**. La ROM n'a pas été exécutée, le cœur 1 est retenu et les resets des périphériques modélisés sont relâchés. Un ELF correctement chargé peut ensuite s'arrêter sur une instruction, un périphérique ou un service ROM non couvert. Les capacités Flash des profils sont fixes : 2 Mio / 4 Mio. Le C++ est traité comme du code machine ; ses types, son runtime et ses bibliothèques ne sont pas émulés séparément.

| Opération | Image / mémoire / cœurs | Points d'arrêt mémoire et instruction | Expressions GUI / inspecteurs |
|---|---|---|---|
| Pause | État conservé | Conservés | Conservées |
| Reset/restart | RAM et modèles réinitialisés, image rechargée, cœur 0 arrêté et cœur 1 retenu | Conservés | Expressions conservées ; fenêtres actualisées |
| Chargement réussi d'un ELF ou de l'exemple | Nouvelle image à l'arrêt | Effacés | Conservées dans le même profil ; expressions réévaluées |
| Chargement refusé | Ancienne session conservée, exécution arrêtée | Conservés | Ancienne session affichée |
| Changement de puce | Nouvelle session vide ; recharger l'ELF | Effacés | Expressions conservées, inspecteurs fermés |
| Fermeture de l'application | Session non sauvegardée | Non sauvegardés | Non sauvegardées |

Une expression conservée après changement d'image peut devenir indisponible ou désigner autre chose : la vérifier ou la supprimer. Le reset est celui du modèle ; il ne représente pas tous les domaines de reset et de rétention d'une puce réelle. Aucun mécanisme d'enregistrement/relecture complète d'une session n'est fourni.

## 10. Erreurs, limites et dépannage Qt

| Situation | Interprétation et action |
|---|---|
| **NON ÉMULÉ**, « reset requis » | Limite du moteur, pas preuve d'un défaut du firmware. Relever instruction/adresse/périphérique ; faire Ctrl+R ou charger une autre image avant de reprendre. |
| **FAUTE CPU**, entrée HardFault | Sur RP2040, le modèle a détecté une faute et préparé le handler lorsqu'il peut le faire ; la reprise est possible si le message l'indique. Inspecter PC, LR, SP et la pile. |
| **LOCKUP** | Échec fatal dans le modèle d'exception ; reset nécessaire. |
| Cœur **RETENU** | Choisir un cœur démarré ou le lancer explicitement. |
| Cœurs endormis, aucun événement | Pas de source d'événement modélisée susceptible de les réveiller ; les périphériques externes absents n'en produisent pas. |
| Erreur d'expression | Vérifier syntaxe, symbole, alignement et couverture. Cette erreur d'inspection n'injecte pas une faute dans le CPU. |
| Mémoire MMIO en `??` | Utiliser l'inspecteur ou `read32` aligné ; les lectures octet MMIO sont hors couverture. |
| Firmware SDK qui s'arrête immédiatement | Vérifier Boot ROM, horloges, instruction Thumb-2/FPU ou périphérique absent. Le chargement d'un ELF ne garantit pas son exécution complète. |

Un accès interrompu peut avoir des effets partiels ; le moteur ne garantit pas une annulation transactionnelle de l'instruction. CFSR/HFSR/BFAR M33 ne sont pas simulés : le diagnostic logiciel n'est pas un relevé de ces registres matériels. Consulter [la couverture](COVERAGE.md) avant d'interpréter un arrêt comme un comportement garanti de la cible.

Pour les avertissements Qt :

```sh
./build-gui/pico-debugger --qt-diagnostics
```

Cette option affiche versions Qt, plateforme, méthode de saisie et, sous Linux/glibc, une pile d'appel pour les messages `QSocketNotifier` / `requestActivate`, y compris avant la création de la fenêtre. Elle ne filtre pas les avertissements.

La correction des listes diffère le rétablissement du focus. Pour Qt5/Wayland avec `QT_IM_MODULE=ibus`, le démarrage laisse Qt choisir sa saisie Wayland native et son repli `compose`, afin d'éviter la création prématurée du watcher IBus. Les fonctions spécifiques d'IBus ne sont alors pas garanties. Pour conserver ce plugin, avec son avertissement éventuel :

```sh
./build-gui/pico-debugger --qt-diagnostics --qt-system-input
```

Aucun réglage du bureau n'est modifié. Les tests automatisés sont effectués en `offscreen` ; ils ne remplacent pas une validation sur le compositeur utilisé. Les détails et références figurent dans [BOOT_AND_PERIPHERALS.md](BOOT_AND_PERIPHERALS.md#wayland-et-qt).

### Options de lancement de la GUI

| Option | Effet |
|---|---|
| `[projet\|elf]` | Dossier CMake à ouvrir dans l'IDE ou ELF à ouvrir dans le débogueur |
| `--debugger` | Ouvrir le débogueur directement |
| `--ide` | Ouvrir l'IDE en premier, comme au lancement sans argument |
| `--chip rp2040` / `--chip rp2350` | Profil ; défaut RP2040 |
| `--demo` | Exemple intégré à l'arrêt ; prend priorité sur le fichier positionnel si les deux sont fournis |
| `-h`, `--help` | Aide ; `--help-all` inclut les options Qt |
| `-v`, `--version` | Version issue de CMake |
| `--qt-diagnostics` | Diagnostic des avertissements Qt au démarrage et pendant la session |
| `--qt-system-input` | Préserver la sélection de saisie système sous Qt5/Wayland |
| `--smoke-test` | Ouvrir puis quitter automatiquement ; destiné à la validation, pas au débogage interactif |

## 11. Cas d'usage

| Objectif | Parcours conseillé | Limite à garder en tête |
|---|---|---|
| Comprendre des instructions et flags Thumb | Exemple intégré, F10, registres et assembleur | Sous-ensemble d'instructions, pas un cours de conformité ARM complet |
| Trouver une écriture inattendue | Watchpoint sur la plage concernée, F5, inspection de PC/pile/mémoire | Arrêt après l'instruction ; pas de déroulage C++ |
| Vérifier un algorithme compilé | ELF autonome compatible, breakpoints, expressions et comparaison mémoire | Runtime/instructions requis doivent être couverts |
| Observer les deux cœurs | Lancement explicite, expressions liées aux cœurs et SRAM partagée | Pas de ROM multicœur ni de modèle exhaustif de concurrence |
| Étudier un registre matériel | Catalogue, filtre adresse/champ, fenêtre indépendante et vue Bits | Une description ne fournit pas un état si le bloc n'est pas émulé |
| Tester un pilote UART en polling | Exemple périphériques, sortie TX, injection RX | Pas de timing baud, DMA, modem ni erreurs de trame complètes |
| Observer timer/PWM/GPIO | Mini-fenêtres simultanées, pas et pause | Temps virtuel, latches/compteurs ; pas de broches physiques |

Le mode émulation ne convient pas encore à la validation complète d'un produit, aux applications Pico SDK arbitraires, au Wi-Fi/Bluetooth, aux contraintes temps réel physiques ou à l'inspection des variables C++ via DWARF.
