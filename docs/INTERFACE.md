# Se repérer dans l'interface

Cette organisation remplace l'agencement du tag v0.3.0. Les outils secondaires apparaissent à la demande ; les fonctions de débogage restent disponibles.

## Les zones de travail

- **CPU, à gauche** : un tableau compare les registres des deux cœurs, avec leurs flags au-dessus. Le point dans l'en-tête indique le cœur sélectionné pour F10, l'assembleur et la pile. Les changements sont surlignés ; les valeurs hexadécimales restent complètes.
- **Espace central** : **Code** suit l'instruction courante ; **Périphériques** contient le catalogue et sa recherche. Chaque inspecteur ouvert ajoute un onglet nommé, sans créer de fenêtre supplémentaire.
- **Inspection, en bas** : **Mémoire**, **Pile**, **Console** et **Carte mémoire** partagent le même espace. La vue Source C++ reste un emplacement réservé, accessible par le menu Vues.

La colonne **Surveillance** est masquée au démarrage. Ajouter une expression, un breakpoint ou un watchpoint l'ouvre sur le bon onglet. **Vues → Expressions surveillées** et **Vues → Points d'arrêt / watchpoints** l'affichent aussi. Les onglets indiquent les nombres d'éléments. Fermer le panneau le masque sans supprimer ses données.

Sans ELF, la page d'accueil propose **Ouvrir un ELF** et **Essayer l'exemple**. Avec une image, l'en-tête indique le fichier, l'état d'exécution, le contexte et le motif d'arrêt. La mention « simulation partielle » reste visible ; les diagnostics complets sont conservés dans la console et la barre d'état.

## Inspecter sans multiplier les fenêtres

1. Faire **Ctrl+P**, chercher un nom, une adresse ou un champ, puis sélectionner un périphérique. Une recherche sans résultat l'indique ; effacer le filtre rétablit le catalogue.
2. Double-cliquer le bloc ou choisir **Inspecter**. Un onglet s'ouvre. Si ce périphérique est déjà ouvert, l'application retrouve son inspecteur.
3. Choisir un registre, puis **Bits** pour ses champs et significations ; **Registres** revient à la liste. Le double-clic sur un registre ouvre aussi ses bits. Le filtre reste utilisable dans les deux vues. Les descriptions longues sont disponibles en infobulle.
4. Pour UART0/1, l'onglet interne **UART · TX / RX** contient la sortie et l'injection. L'onglet **Registres** contient les états et leurs champs. L'injection met toujours l'exécution en pause.
5. Pour voir l'assembleur et le périphérique simultanément, cliquer **Détacher**, puis revenir à **Code**. L'inspecteur devient une fenêtre indépendante, déplaçable sur un autre écran.

**Rattacher**, ou fermer la fenêtre détachée, ramène le même inspecteur dans son onglet : contexte cœur, filtre, sélection et texte RX non envoyé sont conservés. **Rassembler** ou **Ctrl+Maj+P** rattache tous les inspecteurs. La croix d'un onglet périphérique ferme cet inspecteur ; les onglets Code et Périphériques restent disponibles.

Pour comparer deux contextes du même périphérique, utiliser **⋯ → Dupliquer l'inspection**, puis choisir un autre cœur dans la copie. Chacune peut être détachée. L'ouverture normale évite les doublons accidentels ; la duplication reste explicite.

Un changement de carte ferme tous les inspecteurs, onglets et fenêtres. Reset et chargement d'un autre ELF dans le même profil actualisent les vues existantes.

## Garder le code visible avec une fenêtre de périphériques

**Vues → Fenêtre des périphériques…** (`Ctrl+Maj+I`) ouvre un espace indépendant. Le bouton **↗ Fenêtre** du catalogue intégré donne le même accès. L'ouverture répétée retrouve cette fenêtre ; elle ne remplace pas l'onglet actif de la fenêtre principale. Conserver **Code** (`Ctrl+1`) pour suivre l'assembleur en parallèle.

- **Vue générale** : catalogue filtrable à gauche, registres et détails du registre sélectionné à droite. Double-cliquer un bloc ou choisir **Isoler ce périphérique** passe à sa vue dédiée.
- **Périphérique précis** : le choix **Vue** masque le catalogue pour donner toute la largeur aux registres. Les champs de bits et leurs significations restent visibles sous le tableau, avec un séparateur redimensionnable. Tous les registres décrits restent accessibles par défilement et filtre ; la couverture des descriptions n'est pas exhaustive pour tous les blocs.
- **Contexte cœur** : indépendant de celui utilisé pour F10 et l'assembleur. Les valeurs suivent les pas, exécutions, resets et avances du temps sans effet de bord.
- **UART0/1** : un onglet TX/RX apparaît pour le périphérique sélectionné. Changer de vue conserve le texte non envoyé de chaque UART. Injecter RX met l'exécution en pause.
- **Carte et câblage** : rappelle la carte et ses connexions. Sur W, l'entrée CYW43 mène à ces informations ; son état radio reste non émulé.

Fermer cette fenêtre ne change pas les onglets de l'application. **Rassembler** concerne les inspecteurs détachés depuis les onglets ; cette fenêtre autonome reste indépendante. Un changement de carte la ferme. Les tableaux en onglets conservent leur navigation compacte Registres/Bits.

## Temps virtuel et cartes

**Ctrl+6** ouvre les commandes d'avance en µs/ms, avec exécution du firmware ou CPU figés. Le temps courant reste visible et cliquable dans la barre d'état. **Ctrl+7** affiche le câblage de la carte choisie. Voir [le mode d'emploi du temps](VIRTUAL_TIME.md) et [les quatre cartes](BOARDS.md).

## Navigation rapide

| Raccourci | Action |
|---|---|
| F10 | Une instruction du cœur sélectionné |
| F5 / Maj+F5 | Exécuter / mettre en pause |
| Ctrl+R | Reset/restart |
| F9 | Ajouter un breakpoint d'instruction |
| Ctrl+E | Ajouter une expression surveillée |
| Ctrl+1 | Code / assembleur |
| Ctrl+2 ou Ctrl+P | Catalogue des périphériques |
| Ctrl+3 | Mémoire |
| Ctrl+4 | Pile du cœur sélectionné |
| Ctrl+5 | Console et `print` |
| Ctrl+6 | Temps virtuel |
| Ctrl+7 | Carte et câblage |
| Ctrl+Maj+I | Fenêtre indépendante des périphériques |
| Ctrl+Maj+P | Rassembler les inspecteurs détachés |
| Ctrl+Maj+0 | Rétablir la disposition initiale, sans fermer les inspecteurs |

Les autres vues et les watchpoints restent accessibles par **Vues** et **Débogage**. Dans **Surveillance → Arrêts**, **+ Arrêt** ajoute un breakpoint et **+ Accès** un watchpoint mémoire. Un double-clic dans la carte mémoire ouvre la plage correspondante dans Mémoire.

Les séparateurs permettent de donner plus de place au code ou à la mémoire. Sur petit écran, masquer Surveillance lorsqu'elle n'est pas nécessaire. Sous Qt5/Wayland, utiliser **Détacher** dans les inspecteurs : le détachement des docks par glisser-déposer reste désactivé. La disposition n'est pas enregistrée entre deux lancements.

Voir le [tutoriel complet](TUTORIAL.md) pour les expressions, points d'arrêt, erreurs et exemples. Cette réorganisation ne change pas la couverture CPU/périphériques ni le mode de simulation.
