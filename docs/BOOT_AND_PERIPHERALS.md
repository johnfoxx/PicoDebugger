# Démarrage ELF, périphériques et fenêtres d'inspection

## Un ELF peut-il fonctionner sans Boot ROM ?

Oui, avec le **démarrage direct** proposé par ce débogueur. Le chargeur installe les segments ELF en Flash/SRAM, lit le SP initial et le Reset_Handler dans la table des vecteurs lorsqu'elle est identifiée, puis laisse le cœur 0 à l'arrêt. Le firmware exécute ensuite son propre startup. Sans vecteurs identifiés, le modèle utilise `e_entry` et le sommet de SRAM, ce qui est signalé dans le diagnostic.

Ce mode ne reproduit pas la séquence d'alimentation de la puce. La ROM n'est pas exécutée, le cœur 1 reste retenu, les lignes RESETS commencent relâchées et les périphériques avancent en temps virtuel. Cela permet d'exécuter et d'inspecter des programmes autonomes ; cela ne suffit pas à assurer le fonctionnement d'un firmware Pico SDK arbitraire. Les fonctions ROM, la configuration d'horloges, le boot sécurisé RP2350, certaines instructions Thumb-2/FPU et les exceptions M33 restent des obstacles possibles.

Le bandeau de la GUI indique ce mode en permanence. Après chargement/reset, le journal et la barre d'état donnent PC, SP et VTOR. Choisir le profil Pico 2 avant de charger un ELF RP2350, ou utiliser `--chip rp2350`. Les attributs ARM de l'ELF ne sélectionnent pas automatiquement la puce.

## Démonstration reproductible

Depuis le dossier du projet, après compilation :

```sh
python3 examples/make_peripheral_demo.py build-gui/rp2350-peripherals.elf --chip rp2350
./build-gui/pico-debugger --chip rp2350 build-gui/rp2350-peripherals.elf
```

L'ELF contient de véritables instructions Thumb et une table de vecteurs. Il configure UART0, émet `OK`, démarre PWM0, positionne les latches GPIO25, attend une alarme TIMER0 par polling, stocke le compteur à `0x20000000`, puis s'arrête sur `BKPT #3`. Utiliser F10 pour les instructions ou F5 pour atteindre cet arrêt. Une variante `--chip rp2040` est disponible dans le générateur.

Cette image est un exemple de démarrage direct pour le débogueur ; elle n'est pas fournie comme une image UF2/Pico SDK amorçable sur carte. Le test `elf_peripheral_boot` vérifie son chargement et son exécution sur les deux profils. Le test GUI peut aussi vérifier sa sortie UART et l'injection RX :

```sh
QT_QPA_PLATFORM=offscreen ./build-gui/picodbg_gui_tests build-gui/pico2-live.png build-gui/rp2350-peripherals.elf
```

## Petites fenêtres

Dans le catalogue des périphériques, **double-cliquer sur un bloc**, ou le sélectionner et utiliser **↗ Fenêtre**. Plusieurs fenêtres peuvent rester ouvertes en même temps. Chaque fenêtre possède son propre sélecteur de cœur, conserve son périphérique et se rafraîchit avec le modèle. Fermer une fenêtre ne ferme pas les autres. Un changement de puce ferme les fenêtres de l'ancienne cible pour éviter de présenter des adresses d'un autre profil.

Les fenêtres UART0/UART1 comprennent la sortie TX (jusqu'aux derniers 64 Kio environ) et une entrée RX en texte UTF-8. **Injecter RX** met l'exécution en pause et indique le nombre d'octets acceptés. UARTEN/RXE doivent être activés et la FIFO doit avoir de la place. Aucun retour à la ligne n'est ajouté automatiquement. Lire les registres à l'écran ne consomme jamais les octets de la FIFO.

`Ctrl+P` conserve l'explorateur général détachable. `Ctrl+Maj+0` restaure l'agencement principal. Les petites fenêtres restent indépendantes de cet agencement.

## Modèles ajoutés après v0.2.0

### Inspection détaillée des registres

Le catalogue décrit désormais **587 registres / 1 653 champs pour le RP2040** et **783 registres / 2 373 champs pour le RP2350**, alias DMA compris. Ces nombres mesurent la documentation intégrée, **pas la couverture d'émulation**. Les familles ajoutées sont ADC, I2C0/1, DMA (12/16 canaux et leurs alias) et PIO0/1/2 ; elles restent sans modèle d'exécution. Leurs adresses, accès et champs sont consultables, mais leur valeur reste « Non émulé ». La valeur de reset documentaire n'est jamais utilisée comme état courant.

Dans le panneau **Périphériques**, double-cliquer sur un bloc ou utiliser **↗ Fenêtre** ouvre une fenêtre indépendante, déplaçable et redimensionnable, y compris sur un autre écran. Plusieurs fenêtres peuvent observer le même bloc ou des blocs différents ; leur contexte cœur est indépendant. Elles sont déjà détachées de l'agencement principal. Le catalogue complet reste détachable avec `Ctrl+P`.

Dans chaque fenêtre, sélectionner un registre affiche ses champs : positions des bits, valeur numérique et signification. Exemples : `UARTFR.RXFE=1` signifie « FIFO RX vide : Oui », `UARTLCR_H.WLEN=3` signifie « 8 bits ». Les descriptions matérielles incluent parfois des modes non émulés ; la couverture du bloc et ses limites restent applicables. Les listes de valeurs symboliques sont accessibles dans les infobulles, même si la valeur actuelle est inconnue. Les bits absents d'une description ne sont pas automatiquement considérés comme réservés.

Le bouton **Bits** replie ce détail ; un double-clic sur un registre le déplie. Le filtre de registres accepte un nom, une adresse ou un champ, par exemple `RXFE`, `CH15` ou `0x40070018`. Les changements sont surlignés. Les détails réutilisent la valeur déjà inspectée, sans lecture MMIO supplémentaire. Aucun champ n'est éditable dans cette version.

| Bloc | Couverture fonctionnelle | Limites principales |
|---|---|---|
| TIMER0/TIMER1 RP2350 | Compteurs indépendants, alarmes, IRQ0..7, PAUSE, LOCKED, SOURCE, alias RW | Tick et clk_sys virtuels ; pas de bloc TICKS/arbre d'horloge |
| UART0/UART1, deux puces | FIFO RX/TX, états, seuils et IRQ, loopback interne, injection RX, capture TX, dépassement RX, timeout virtuel | Pas de baud physique, pinmux, modem, IrDA, DMA ni simulation complète des erreurs de trame ; saturation TX provoque un arrêt explicite |
| SPI0/SPI1, deux puces | FIFO 8 mots, maître Motorola 4–16 bits, loopback, masques/états IRQ, overflow et timeout virtuel | Pas d'esclave externe, mode esclave, DMA ni signal électrique ; émission hors loopback refusée |
| PWM, deux puces | 8/12 slices, compteur/diviseur fractionnaire ≥1, phase correcte, CC/TOP bufferisés, wrap, masques/forçages IRQ, activation groupée | Pas de broches, DREQ, entrée B, ajustements PH_ADV/PH_RET ni diviseurs spéciaux <1 ; CC est mémorisé sans forme d'onde externe |
| RESETS | Registres RESET/WDSEL/RESET_DONE, alias, remise à zéro et maintien en reset des modèles timer/UART/SPI/PWM | Complétion immédiate, état initial du démarrage direct ; ne modélise pas le reset global, les domaines d'alimentation ou les blocs absents |
| SIO RP2350 | Latches GPIO_OUT/OE bas/hauts et SET/CLR/XOR aux adresses propres au Pico 2 ; CPUID/FIFO/spinlocks conservés | Pas d'entrées électriques, pinmux, GPIO sécurisés/non sécurisés, interpolateurs ni doorbells |
| PPB RP2350 | SysTick virtuel, VTOR, banques NVIC enable/pending pour IRQ0..51, registres distincts par cœur | Pas de priorités, préemption, TrustZone ni **entrée/retour d'exception M33** ; une IRQ prise arrête explicitement le moteur |
| WATCHDOG | SCRATCH0..7 et alias RW | Pas de compteur/expiration/reset watchdog ; le restart du débogueur réinitialise aussi les scratch |

Le modèle UART mémorise les interruptions de seuil lors d'un franchissement ; activer un UART vide ne fabrique pas d'IRQ TX. Les acquittements et les lectures CPU sont séparés de l'inspection. Les timeout série sont exprimés en quanta du moteur, pas en périodes de bits.

SPI, PWM et UART sont maintenant dans des modules séparés du routage `PeripheralBus`. Les accès inconnus et les modes absents provoquent `Unsupported` avec l'adresse du registre ; ils ne retournent pas une valeur zéro arbitraire. Les fenêtres indiquent la couverture de chaque bloc. I2C, ADC, DMA, PIO, USB, horloges et boot sécurisé restent à implémenter ; voir [COVERAGE.md](COVERAGE.md).

## Wayland et Qt

Le message `Wayland does not support QWindow::requestActivate()` est émis par le backend Wayland de Qt5 lorsqu'une activation est demandée. Qt5 appelle notamment `activateWindow()` à la fin d'un glisser-déposer de dock flottant. Sources : [backend QtWayland 5.15](https://github.com/qt/qtwayland/blob/5.15/src/client/qwaylandwindow.cpp), [QDockWidget Qt 5.15](https://github.com/qt/qtbase/blob/5.15/src/widgets/widgets/qdockwidget.cpp).

La première correction ne couvrait pas les listes déroulantes. Le cas de sélection de puce a été reproduit en traçant les appels Qt : `QComboBox::hidePopup()` → fermeture du popup → rétablissement du focus → `QWindow::requestActivate()`. Le nouveau `ChoiceBox` conserve le popup natif et diffère le retour du focus clavier jusqu'à ce que la fenêtre propriétaire ait réellement retrouvé le focus du compositeur. Les sélecteurs de puce, de cœur et les dialogues utilisent ce composant.

Le projet ouvre aussi les fenêtres d'inspection sans demander l'activation sous Wayland et évite `raise()`. Sous Qt5/Wayland, le détachement d'un dock par glisser-déposer reste désactivé pour éviter un autre appel interne de Qt ; `Ctrl+P` et les petites fenêtres restent accessibles. Les avertissements Qt ne sont pas filtrés. Le test Linux/Qt5 `qt_popup_activation` intercepte les appels à `QWindow::requestActivate()` uniquement dans son exécutable, les transmet à Qt et vérifie l'absence de demande lors des choix souris/clavier, de l'annulation et du retour différé du focus. Cette validation reste **offscreen**, sans accès au compositeur Wayland depuis l'environnement de développement.

Le second avertissement, `QSocketNotifier: Can only be used with threads started with QThread`, a été identifié grâce à la trace fournie sur la machine utilisateur : le plugin **IBus** de Qt5 crée un `QFileSystemWatcher` dans le constructeur de l'intégration Wayland, avant la création du dispatcher d'événements. Ce chemin est visible dans les [sources QtWayland 5.15](https://github.com/qt/qtwayland/blob/5.15/src/client/qwaylandintegration.cpp).

Pour ce cas précis (Qt5, session/plateforme Wayland, `QT_IM_MODULE=ibus`), le démarrage retire cette sélection **dans le processus du débogueur uniquement**, afin que Qt choisisse son contexte de saisie Wayland natif et son repli `compose`. Le clavier et la composition disponibles dépendent alors du compositeur ; les fonctionnalités propres au plugin IBus ne sont plus garanties. `--qt-system-input` conserve la configuration IBus d'origine, avec son avertissement éventuel. Les autres méthodes de saisie, X11 explicitement choisi et Qt6 conservent leur configuration. Aucun réglage de session ni paquet système n'est modifié.

Pour identifier un autre avertissement, le diagnostic s'installe avant `QApplication` et ajoute une pile d'appel aux messages ciblés sous Linux/glibc :

```sh
./build-gui/pico-debugger --qt-diagnostics
# Comparaison avec le plugin de saisie système :
./build-gui/pico-debugger --qt-diagnostics --qt-system-input
```

Qt5 suffit pour utiliser cette version. CMake sait sélectionner Qt6 si son SDK est installé, sans changer le moteur C++11 ; l'interface Qt6 utilise C++17. Sélection explicite, dans un dossier de build distinct :

```sh
cmake -S . -B build-qt6 -DPICODBG_GUI=ON -DPICODBG_QT_MAJOR=6
cmake --build build-qt6 -j2
ctest --test-dir build-qt6 --output-on-failure
```

`PICODBG_QT_MAJOR=5` impose Qt5 ; `AUTO` reste la valeur par défaut. Qt6 n'a pas été installé ni testé dans cette session. La version affichée par `--version` provient désormais de CMake au lieu d'une ancienne chaîne codée en dur.
