# Piloter le temps virtuel

Ouvrir **Vues → Temps virtuel** (`Ctrl+6`), ou cliquer sur **t = … µs** dans la barre d'état. Choisir une durée en microsecondes ou millisecondes, un mode, puis **Avancer**. Le panneau affiche le temps écoulé et la durée restante. **Pause** ou **Maj+F5** annule la durée restante ; une nouvelle avance repart de l'instant courant.

| Mode | Effet |
|---|---|
| Exécuter le firmware | Exécute les cœurs démarrés jusqu'à la durée demandée, avec l'ordonnanceur habituel. Un breakpoint, watchpoint, BKPT, une faute, une instruction non couverte ou une attente sans événement peut arrêter plus tôt. |
| CPU figés · périphériques seuls | Avance timers, UART, SPI, PWM et SysTick sans exécuter une instruction. Les registres CPU restent inchangés ; les IRQ deviennent pendantes et seront traitées lors de la reprise, dans les limites du modèle CPU. |

Les mises à jour sont faites par tranches courtes sur la boucle Qt : la fenêtre indépendante des périphériques se rafraîchit et la commande Pause reste accessible pendant une longue avance. Une injection UART met également fin à l'avance en cours. Reset, chargement d'un nouvel ELF ou changement de carte annulent la durée restante.

## Exemple avec un timer

1. Charger l'exemple intégré et faire **Ctrl+R**.
2. Ouvrir **Ctrl+Maj+I**, choisir **TIMER** sur Pico/Pico W ou **TIMER0** sur Pico 2/Pico 2 W. Filtrer les registres sur `TIMERAWL`.
3. Dans **Ctrl+6**, saisir `25`, choisir `µs` et **CPU figés · périphériques seuls**, puis **Avancer**.
4. Le temps affiche `25 µs`, TIMERAWL vaut `0x00000019` et le PC reste `0x10000200`.
5. Choisir **Exécuter le firmware**, durée `3 µs` : le temps atteint `28 µs`, trois instructions ont été exécutées, R0 vaut `1` et PC `0x10000206`.

L'avance avec CPU figés permet notamment d'observer une alarme programmée par le firmware sans continuer le code. Elle ne remplace pas le mode firmware pour un clignotement piloté par une boucle CPU : sans exécution, le code ne peut pas commuter sa LED.

## Sémantique et limites

- Le compteur global de session revient à zéro au chargement/reset. Un ELF rejeté laisse l'ancien état, dont le temps, intact.
- Un quantum exécuté ou d'attente vaut **1 µs virtuelle** ; un créneau sauté parce qu'un cœur est retenu ne fait pas avancer le temps. Deux cœurs partagent ce compteur ; ils ne possèdent pas deux horloges indépendantes.
- L'horloge de session est distincte des registres TIMER : le firmware peut les réécrire ou les mettre en pause. L'avance respecte le registre PAUSE et les resets des périphériques.
- Les lectures d'inspection et la pause n'avancent jamais le temps. L'avance explicite avec CPU figés utilise les mêmes quanta que l'exécution, sans simuler les signaux d'arrêt du debug matériel : DBGPAUSE n'est pas appliqué par l'ordonnanceur.
- Il n'y a ni retour arrière, ni synchronisation avec l'heure réelle, ni facteur de vitesse matériel. Une microseconde virtuelle n'est pas une promesse d'équivalence aux cycles de l'ARM ou au débit baud.
- Les exceptions M33 restent non émulées : voir une IRQ pendante sur Pico 2 ne garantit pas l'exécution de son handler.

Le moteur fournit `Debugger::runFor(durée, budget)` et `Debugger::advanceTime(durée)` ; les interfaces graphiques fractionnent les longues demandes. L'avance directe est limitée à 1 000 000 µs par appel moteur et ne contourne pas un arrêt terminal.
