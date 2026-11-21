# Architecture et maintenance

## Frontières

`picodbg_core` est du C++11 sans Qt. `Memory` possède les octets et les périphériques ; chaque `Cpu` son contexte architectural. `Debugger` possède le bus, les deux CPU, l'ELF, les points d'arrêt et la politique d'exécution. `Debugger` implémente maintenant `DebugTarget`, le contrat C++11 commun aux deux modes. Les vues Qt passent par `peek`, `disassemble`, les mutations de points et les indicateurs de disponibilité de ce contrat, sans lire directement `Memory`.

Le contrôleur est non copiable et à propriétaire unique. Qt exécute des tranches courtes puis rafraîchit les panneaux sur son thread principal ; aucun thread concurrent ne lit un CPU en cours de mutation. Un futur worker devra publier des snapshots immuables des deux cœurs et des plages mémoire plutôt que partager les objets mutables.

## Backend matériel

`GdbTarget` (couche Qt) implémente `DebugTarget` avec les instantanés obtenus par GDB ; il ne possède pas de mémoire ni de bus émulé. `GdbMi` pilote un `QProcess` en MI2, sans shell ni scripts GDB autochargés, avec une requête tokenisée en cours et une file bornée. Les notifications `*running`/`*stopped` sont indépendantes des résultats de commande. Une expiration ou perte de processus invalide la session. OpenOCD est soit un processus possédé, soit un serveur externe que l'application ne termine pas.

Connexion, upload et inspections sont asynchrones sur le thread Qt. Le timer matériel interroge l'état des opérations et rafraîchit les vues à chaque révision. Après arrêt, le cache de registres GDB est invalidé, les threads physiques sont identifiés, puis leurs registres sont lus séparément. Les registres absents ne sont jamais remplis depuis le modèle. Les lectures mémoire demandées par les vues sont mises en cache ; les callbacks portent une génération, afin d'ignorer une réponse devenue obsolète après une mutation. Une erreur de lecture est également mémorisée, sans boucle de nouvelles lectures.

Les accès MMIO automatiques sont limités à une liste revue. `readRegisterOnce` autorise une lecture explicite, potentiellement destructive, réutilisée par le tableau et les champs. `peek` signifie donc inspection contrôlée du backend, et n'implique pas qu'une lecture physique soit intrinsèquement sans effet. Le catalogue décrit les registres ; il ne certifie pas leur innocuité sur silicium.

`load` charge les symboles uniquement en mode réel. `download` vérifie le fichier, les plages de chargement et les vecteurs RAM avant toute séquence de programmation. Les écritures de registres utilisent les numéros retournés par GDB, pas des variables de convenance `$nom` susceptibles de masquer un registre absent. Un transfert incomplet bloque la reprise. Les breakpoints/watchpoints ne sont ajoutés aux listes qu'après acceptation du serveur ; la disponibilité finale des comparateurs peut être signalée à la reprise par GDB.

## Invariants du simulateur

- Décodage et désassemblage partagent la même représentation `Instruction`.
- Adresses en `uint32_t`, calculs de limites en 64 bits. Vérifier les débordements avant accès.
- Calculs architecturaux en non signé modulo 2³² ; décalages ≥ 32 traités explicitement.
- `Memory::read/write` : accès CPU ; `peek` : inspection sans effet de bord. Préserver cette distinction pour tout périphérique.
- `Unsupported` signifie « le modèle ne sait pas », jamais une faute matérielle. Pas de lecture zéro fictive pour un périphérique absent.
- Valider un nouvel ELF sur un bus temporaire avant de remplacer la session.
- Breakpoint avant instruction, watchpoint après accès ; pas manuel autorisé sur un breakpoint.
- Chargement toujours à l'arrêt, sans exécution automatique.

## Extensions

Instruction : documenter encodage/contraintes, ajouter `Op` et décodeur, exécution, puis tests des valeurs limites et flags préservés. Une référence indépendante (assembleur ARM, modèle de référence, carte) est nécessaire pour affirmer la conformité.

Périphérique : plage/couverture dans `chip.cpp`, accès CPU/inspection séparés, puis tests des largeurs, alias, effets de lecture, IRQ et reset. Lorsque les modèles grossiront, extraire `Peripheral`, `Sio`, `SystemControl` dans leurs propres modules. La cartographie contient des fenêtres de décodage, parfois avec alias/réserves : elle n'autorise pas tous les accès d'une fenêtre.

`timer.hpp/.cpp` est le premier module de comportement séparé : il reçoit des quanta virtuels, sans dépendance à Qt ni à l'horloge hôte. `Memory` décode ses accès et distribue ses lignes IRQ aux deux contextes système. `Debugger` avance le temps une seule fois par quantum, puis évalue les sources propres à chaque cœur. Les tests couvrent aussi le cas où tous les cœurs exécutables attendent une alarme.

`peripherals.hpp/.cpp` décrit le catalogue matériel, les registres connus et leurs champs ; ces métadonnées sont séparées du comportement. `PeripheralPanel` en Qt lit uniquement `peek` : aucun rafraîchissement ne doit acquérir un spinlock, consommer une FIFO, acquitter une IRQ ou capturer la partie haute du timer. Il reconstruit son catalogue au changement de cible et conserve ses lignes lors des rafraîchissements ordinaires. Le panneau sert de catalogue central ou de contenu fixé à un périphérique dans un inspecteur.

L'extension Pico 2 extrait le routage des modèles partagés dans `PeripheralBus` : timers RP2040/RP2350, `Uart`/`Spi` PL011/PL022, `Pwm`, reset et latches GPIO/scratch. Les dispositifs série/PWM signalent des offsets de registre ; le bus les convertit en adresses cibles absolues pour les diagnostics. Les lignes IRQ sont stockées sur 64 bits afin de représenter notamment UART0/1 sur IRQ33/34 du RP2350. Les registres NVIC exposent les deux banques sans prétendre implémenter les exceptions M33.

`PeripheralInspection` regroupe un `PeripheralPanel`, son contexte cœur et, pour un UART, une vue TX/RX. `InspectionWorkspace` possède les onglets et fenêtres et déplace le même widget entre eux, sans recréer son état. La fermeture d'une fenêtre rattache sa page ; la fermeture d'un onglet supprime l'inspecteur. Les entrées sont retirées avant les suppressions différées pour empêcher un callback de fermeture de ressusciter une page au changement de profil. Le contrôleur conserve uniquement des `QPointer` vers les inspecteurs, vérifiés avant chaque mise à jour. L'ouverture normale réutilise la vue existante ; une action explicite crée une copie avec contexte indépendant. L'injection UART passe par `Debugger::injectUart`, avec pause et révision cohérente.

La fenêtre principale compare les CPU dans un tableau unique et regroupe les vues secondaires en onglets. Les actions de navigation révèlent leur panneau même s'il a été masqué. Le test graphique couvre aussi une fenêtre 1280×800, la lecture complète des valeurs CPU, la navigation, les ouvertures répétées, les détachements/rattachements, les copies et la fermeture au changement de puce.

`window_policy.hpp` centralise l'affichage sans demande d'activation Wayland. La restriction du détachement par glisser-déposer concerne Qt5/Wayland et évite un appel interne à Qt ; elle ne masque aucun avertissement.

`ChoiceBox` conserve le comportement de `QComboBox` et diffère la restitution du focus jusqu'au retour effectif du focus à sa fenêtre propriétaire. `qt_input_policy.hpp` traite le démarrage prématuré du plugin IBus sous Qt5/Wayland avant la création de `QApplication`, avec une option de désactivation du contournement. `qt_diagnostics` est un outil optionnel de trace des avertissements, installé avant les plugins Qt ; il ne filtre aucun message.

Les descriptions complémentaires sont réparties par famille dans `src/core/registers/` (ADC, I2C, DMA, PIO, série). Les champs contiennent position, largeur, description et valeurs symboliques ; le moteur C++11 les décode indépendamment de Qt. Ajouter une description ne change jamais la couverture du modèle ni le comportement du bus. Les tests vérifient adresses uniques, champs sans chevauchement et différences RP2040/RP2350.

`RegisterInspector` est une vue sans accès au bus. `PeripheralPanel` lui transmet la valeur brute du même snapshot que le tableau, ou un état indisponible. Ne jamais remplacer un état inconnu par une valeur de reset. Le filtre agit sur les lignes de registres ; les fenêtres indépendantes conservent leur filtre, leur sélection et leur contexte.

`BoardDescription` décrit Pico/Pico W/Pico 2/Pico 2 W séparément de `ChipDescription`. `Debugger(Board)` choisit le bus RP2040/RP2350 correspondant ; l'ancien constructeur `Debugger(Chip)` reste compatible et sélectionne la carte sans Wi-Fi. Le CYW43 externe est une information de câblage, sans adresse MMIO fictive.

`PeripheralExplorer` possède une fenêtre autonome avec son contexte cœur et son choix de périphérique. Il réutilise `PeripheralPanel` en mode étendu, qui affiche registres et champs dans un splitter, et `UartTerminal`, partagé avec les inspecteurs existants. `BoardPanel` fournit les mêmes informations de carte dans les deux fenêtres. Les callbacks retournent au contrôleur principal pour toutes les mutations.

`Debugger::virtualTime()` compte les quanta depuis reset. `clockQuantum()` applique la même politique aux périphériques et SysTick en pas/exécution et en avance avec CPU figés. `runFor` respecte le budget d'ordonnancement et les arrêts habituels ; `advanceTime` ne fait pas entrer les CPU dans les handlers. `VirtualTimePanel` émet une demande ; le contrôleur l'exécute par petits lots et conserve la durée restante. La commande Pause, un changement de cible ou un chargement annule cette durée. Les tests `boards`, `virtual_time` et `qt_session` couvrent ces comportements, en complément des régressions existantes.

## Correspondance source C++

`sourceHint(address)` fournit un symbole précédent + offset, pas une ligne C++. La GUI réserve une zone à côté de l'assembleur. La prochaine couche devra charger DWARF, gérer chemins de compilation et répertoires fournis par l'utilisateur, puis résoudre adresse → fichier/ligne/colonne.

Prévoir un `SourceResolver` indépendant de Qt. Le déroulage de pile devra utiliser DWARF CFI et les cadres d'exception, plutôt que deviner les retours en scannant des valeurs. La lecture des variables typées et le démanglage sont distincts de la lecture de mots mémoire.

## Validation

Effectuée : GCC C++11, suites CTest, ELF synthétique reproductible, tests ELF liés directement au moteur, AddressSanitizer/UBSan avec détection des fuites désactivée à cause de `ptrace`. Le parcours exhaustif du décodeur teste sa robustesse, pas la sémantique de chaque encodage.

Validation v0.1.0 complémentaire : compilation Qt 5.15, 10 tests CTest réussis dont `qt_smoke` et `qt_interactions`, capture offscreen inspectée. Non effectuée : Qt6, compilation croisée ARM, comparaison au silicium et conformité ARM.

Extension périphériques : 11 tests CTest avec Qt5, dont la suite `peripherals` (latches, débordement, alarmes, alias, IRQ, WFI, inventaire). Le test GUI vérifie aussi les valeurs du timer, le filtrage, l'absence de valeurs inventées, la fenêtre détachée et le changement RP2040/RP2350. Les neuf suites du moteur sont exécutables sous AddressSanitizer/UBSan. Les captures de la vue d'ensemble et de la fenêtre périphériques sont inspectées séparément.

Sur une machine équipée de Qt : construire la GUI, ouvrir l'exemple, vérifier F10/F5/Maj+F5, breakpoint par double-clic, watch `read32(counter)`, lancement cœur 1, reset/changement de profil, agencement sur petit écran. L'inspection doit préserver FIFO/spinlocks.

`scripts/install-qt-debian.sh` installe les dépendances puis appelle `scripts/build-gui.sh`. Celui-ci exige une cible graphique et exécute CTest. `picodbg_widgets` est partagé par l'application et `picodbg_gui_tests`, qui pilote les QAction et signaux des widgets sans accéder aux CPU privés.
