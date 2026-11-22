# Manuel utilisateur de PicoDebugger

PicoDebugger permet d’observer un programme ARM/Thumb exécuté en émulation ou sur une carte reliée par SWD. L’IDE sert à éditer le C, compiler le projet et consulter le désassemblage complet de l’ELF. Le débogueur regroupe l’état des cœurs, les adresses mémoire et les périphériques. Ce manuel est accessible dans **Aide → Manuel utilisateur** du débogueur.

## Démarrer

Vous pouvez commencer dans l’IDE avec `pico-debugger` ou ouvrir directement le débogueur avec `pico-debugger --debugger`. En lui passant le chemin d’un ELF, vous ouvrez cet ELF dans le débogueur. Dans les deux cas, le profil doit correspondre à la cible compilée. Choisissez **Pico**, **Pico W**, **Pico 2** ou **Pico 2 W** dans le sélecteur.

### Éditer, compiler et déboguer dans l’IDE

1. Utilisez **Projet → Nouveau projet…** pour créer un projet bare metal ou Pico SDK. **Projet → Ouvrir un projet…** ouvre un dossier avec un `CMakeLists.txt`. Le modèle bare metal fournit un ELF lié en SRAM compatible avec la démonstration en émulation et le chargement SWD en RAM.
2. Ouvrez le C dans l’arborescence. **Ctrl+S** enregistre le fichier courant et **Ctrl+Maj+S** les fichiers ouverts. **Ctrl+F** et **Ctrl+H** recherchent ou remplacent dans le fichier. Ajoutez **Maj** à ces raccourcis pour travailler dans tout le projet. La languette **TODO** retrouve les tâches dans les sources, et le menu **Documentation** donne accès à Doxygen.
3. Sélectionnez la cible dans la barre d’outils. **Ctrl+B** compile. Ouvrez **Problèmes** pour atteindre les lignes signalées par le compilateur ou clangd. **Compilation → Outils du projet…** permet de régler les chemins des outils.
4. Appuyez sur **F5** pour compiler et charger le firmware à l’arrêt en émulation. Le bouton **Débogueur** réaffiche la fenêtre de la session. **Débogage → Quitter le débogage** ramène à l’éditeur C.

### Utiliser le débogueur seul

Cliquez sur **Ouvrir ELF** ou **Exemple**, puis choisissez le cœur à inspecter. **F10** avance d’une instruction sur le cœur choisi. **F5** lance les cœurs démarrés et **Maj+F5** met l’exécution en pause. **Ctrl+R** réinitialise la cible. La barre d’état indique le mode courant, la carte et l’état de la session. **Fichier → Ouvrir l’IDE C** permet d’afficher l’IDE depuis la session.

### Se connecter à une carte

En mode **Émulation**, le firmware utilise la mémoire et les périphériques modélisés. Leur couverture figure dans le catalogue et dans **Aide → Couverture et limites**.

Pour utiliser la carte réelle, reliez le SWD de la sonde à la carte et ouvrez **Cible → Connecter la sonde…**. Une fois la carte identifiée et arrêtée, ouvrez l’ELF correspondant pour charger les symboles. **Téléverser l’ELF sur la carte** écrit le firmware. Choisissez **Flash** pour un ELF amorçable prévu pour la Flash ou **RAM** pour un ELF lié en SRAM qui possède une table de vecteurs. Depuis l’IDE, **Téléverser et déboguer sur la carte…** compile puis lance ce parcours. L’exécution se commande ensuite dans le débogueur.

## Organiser les panneaux

Avec **Tous**, les registres et les piles des deux cœurs sont visibles à gauche. **Cœur 0** et **Cœur 1** ouvrent un seul contexte et élargissent ses détails. La colonne centrale accueille **Mémoire**, **Périphériques** et, quand le débogueur est utilisé seul, **Assembleur**. **Outils** se replie sous ces vues. **Surveillance** occupe la colonne de droite.

- **Vues → Mettre en avant** permet d’afficher une seule vue centrale, le duo **Mémoire et périphériques** ou toutes les vues disponibles. Les panneaux conservent leur organisation.
- Faites glisser la barre de titre d’un panneau vers un bord pour le déplacer ou vers un autre panneau pour les regrouper en onglets. Faites glisser les séparateurs pour modifier les tailles. Vous pouvez ainsi empiler Mémoire et Périphériques, les placer côte à côte ou ne garder qu’un des deux visible.
- **Vues → Panneaux** affiche ou masque chaque panneau. **Vues → Fenêtres flottantes** détache ou rattache un panneau. Plusieurs panneaux peuvent flotter simultanément.
- La disposition est mémorisée à la fermeture du débogueur. **Ctrl+Maj+0**, dans **Vues → Rétablir la disposition initiale**, restaure les positions et la visibilité de départ.
- Survolez un bouton, un champ ou une valeur pour afficher son rôle. Les menus **Actions** et le clic droit donnent accès aux commandes des tableaux de mémoire et de registres.
- **Vues → Thème** applique le thème clair ou sombre aux fenêtres et aux inspecteurs. Le choix est enregistré.

## Cœurs CPU et pile

### Cœurs CPU

Les registres standard s’affichent sans défilement. En mode **Tous**, le point dans l’en-tête signale le dernier cœur actif pour le pas à pas, les expressions et la navigation. Choisissez **Cœur 0** ou **Cœur 1** dans la barre d’exécution pour afficher une colonne **Signification**. Elle indique les symboles ELF et régions des valeurs qui correspondent à des adresses, ainsi que les drapeaux et bits de contrôle connus. Les infobulles donnent les mêmes indications en mode **Tous**.

Double-cliquez sur une valeur à l’arrêt pour l’éditer dans la colonne du cœur voulu, en décimal ou en hexadécimal avec le préfixe 0x. Le jaune signale les valeurs récemment modifiées. En émulation, seuls les drapeaux implémentés de xPSR et les bits modélisés des registres de contrôle sont inscriptibles. Sur cible réelle, le clic droit ouvre les **Registres supplémentaires** signalés par GDB.

En émulation, **Débogage → Démarrer un cœur explicitement** permet de fournir l’adresse d’entrée Thumb et le pointeur de pile d’un cœur retenu. Sur une carte réelle, les registres des cœurs exposés par GDB sont relevés à l’arrêt.

### Pile

**Piles par cœur** (**Ctrl+4**) affiche une synthèse par cœur. Chaque en-tête identifie une trame et sa fonction. Le panneau Piles occupe la hauteur disponible sous les registres. La trame **#0** est la fonction courante du cœur concerné. La trame **#1** est l’appelante.

La grille aligne ses lignes sur des adresses multiples de 4. Chaque ligne montre quatre octets en hexadécimal, puis leurs quatre caractères ASCII dans le même ordre d’adresses. Les points indiquent les valeurs non imprimables, `??` les lectures indisponibles et les espaces les positions hors trame. L’en-tête donne l’adresse contenue dans le registre **SP**. Le repère **[SP]** désigne les octets stockés à cette adresse. Ainsi, **00 00 00 00** sur cette ligne signifie que le mot en mémoire vaut zéro. **RET** désigne le retour sauvegardé, **FP** l’ancien pointeur de trame si les règles de déroulage permettent de le reconnaître, et **S4**, **S7**, etc. les registres sauvegardés identifiés. La synthèse garde les mots qui contiennent **FP** et **RET** à leur adresse réelle. Une ligne **…** signale les mots intermédiaires omis. **a1**, **a2** désignent les arguments, **l1**, **l2** les variables locales. Les infobulles affichent la correspondance complète.

Double-cliquez sur le nom de la trame pour ouvrir son **inspecteur détaché**. Son bandeau rappelle le cœur, la fonction, le PC, le SP et la base de trame (**CFA**) lorsqu’elle est connue. La grille conserve les mots alignés. À côté, la table détaillée donne le nom, l’offset depuis SP ou le registre, le type, la taille et la valeur décodée. Dépliez les tableaux pour éditer leurs éléments ou parcourez les pages de mémoire pour accéder à leurs octets. Les limites déduites des pointeurs et les aperçus sont indiqués dans le bandeau.

Le bleu indique une sélection manuelle dans le débogueur. Dans la synthèse, cliquez sur un repère, un octet ou son caractère ASCII pour sélectionner la plage correspondante. L’inspecteur synchronise cette sélection avec sa grille et sa table détaillée.

Dans l’inspecteur, cochez **Zone** sur les lignes à mettre en évidence. Sélectionnez une variable puis utilisez **Couleur…** pour lui attribuer une couleur. Le surlignage couvre ses octets sur toutes les lignes et reste propre à l’inspecteur. Les octets sans symbole sont grisés. Les valeurs modifiées apparaissent en gras et soulignées en ambre pendant 2,4 secondes dans l’hexadécimal, l’ASCII et la colonne **Décodé**.

À l’arrêt, double-cliquez sur un octet hexadécimal pour saisir deux chiffres hexadécimaux, ou sur un caractère ASCII pour saisir un caractère ou une séquence telle que `\x00`. Un double-clic sur l’adresse ouvre l’édition du mot en little-endian. Dans la table détaillée, double-cliquez sur **Décodé** pour modifier une valeur typée. La valeur de retour est éditable lorsqu’une instruction de retour et un registre de résultat compatibles sont identifiés. Un inspecteur dont la trame a disparu indique qu’il affiche le dernier arrêt.

Compilez avec `-g` pour obtenir les noms et les types. Dans la convention de base ARM 32 bits, les premiers arguments entiers transitent par **r0–r3** et le compilateur peut les recopier en pile dans le prologue. L’inspecteur affiche le registre ou l’emplacement décrit pour le PC courant. En émulation, un emplacement fixe réservé au prologue indique **Copie en pile non observée** jusqu’à l’écriture correspondante. Sur carte, GDB fournit les variables des trames ouvertes. Les règles DWARF de déroulage servent à identifier les registres sauvegardés, y compris le FP lorsqu’elles permettent de le reconnaître. Certaines trames restent partielles si les informations de débogage manquent ou utilisent des expressions non prises en charge. [Convention Arm AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst#65parameter-passing) · [Spécification DWARF](https://dwarfstd.org/doc/DWARF5.pdf)

Si **RET** n’apparaît pas en mémoire, consultez le registre **LR** et le désassemblage de la fonction. Une fonction peut retourner par **LR** sans l’avoir sauvegardé sur la pile. La présence de **FP** dépend du code généré et des règles disponibles à l’instruction courante. **RV**, lorsqu’il apparaît dans l’inspecteur, désigne la valeur renvoyée par la fonction. **RET** est son adresse de retour.

## Mémoire

**Ctrl+3** révèle la vue mémoire. Saisissez une adresse hexadécimale, un symbole ELF ou une expression telle que `sp + 16`, puis appuyez sur **Entrée** ou **Lire**. Indiquez le nombre d’octets à afficher. Les flèches avancent ou reculent d’une page.

Les adresses de ligne sont à gauche, les offsets et octets hexadécimaux au milieu et l’ASCII à droite. La grille adapte le nombre d’octets par ligne à la largeur du panneau. Cliquez sur un octet ou sur son caractère ASCII pour sélectionner une adresse précise. Les points représentent les caractères non imprimables et `??` les octets indisponibles.

| Indication | Signification |
| --- | --- |
| Adresse bleue | Zone en lecture seule |
| Adresse verte | Zone inscriptible |
| Adresse violette | Ligne couvrant plusieurs modes d’accès |
| Adresse grise | Accès à déterminer ou propre à chaque registre MMIO |
| Fond de couleur sur les octets | Zone, adresse ou périphérique suivi dans Surveillance |

- **Actions → Écrire…** modifie une valeur sur 8, 16 ou 32 bits. Le double-clic sur un octet ouvre le même éditeur. L’écriture nécessite une cible arrêtée et une zone inscriptible. Les valeurs peuvent être saisies en décimal ou avec le préfixe `0x`.
- **Actions → Suivre…** ajoute une valeur sur 8, 16 ou 32 bits, ou une **Plage d’octets** de 1 à 4096 octets, à Surveillance.
- Sur carte réelle, **Actions → Lire le registre MMIO une fois** lit explicitement le mot sélectionné. Certains registres vident une FIFO ou acquittent un indicateur à la lecture.
- En zone MMIO, la ligne de contexte indique le périphérique, sa base et l’offset de l’adresse. Le nom et l’accès du registre s’ajoutent lorsque le catalogue le décrit.

## Périphériques

**Ctrl+2** ouvre le catalogue. Le filtre recherche un périphérique, un registre, un champ ou une adresse. La colonne **Modèle** indique la couverture d’émulation. Le tableau de registres affiche la valeur et l’accès. En élargissant le panneau, vous voyez aussi l’adresse et les champs connus. L’adresse figure également dans l’infobulle du nom du registre.

| Code | Accès |
| --- | --- |
| `RO` | Lecture seule |
| `RW` | Lecture et écriture |
| `WO` | Écriture seule |
| `W1C` | Écrire 1 efface le bit correspondant |
| `RC` | Lecture avec effacement |

- **Bits** détaille les champs du registre sélectionné et leurs valeurs.
- **Actions** ou le clic droit permettent d’écrire ou de suivre ce registre. Un double-clic sur sa valeur ouvre l’éditeur si le registre est inscriptible. **Lire une fois** est disponible sur la cible SWD pour une lecture explicite.
- **Suivre le bloc** ajoute tous ses registres à Surveillance. **Mémoire** ouvre la plage à partir de la base du bloc dans le contexte du cœur courant.
- **Zoom** ouvre un inspecteur de ce périphérique. Il possède son propre sélecteur de cœur, une commande **Détacher** et une action **Dupliquer l’inspection** dans le menu `⋯`. **Ctrl+Maj+P** rassemble les inspecteurs détachés. La fermeture de la fenêtre d’un inspecteur le rattache.
- **Vues → Périphériques → Explorateur indépendant…** (**Ctrl+Maj+I**) ouvre une fenêtre avec registres et champs affichés ensemble.

Les inspecteurs UART exposent un onglet **TX / RX** pour voir les octets transmis et injecter des caractères en émulation. Sur carte réelle, utilisez **Cible → Moniteur série de la sonde** (**Ctrl+Maj+U**) pour la liaison série.

## Surveillance

### Suivis

Suivez plusieurs blocs, registres, valeurs ou plages SRAM. Un groupe se déplie pour afficher les registres du bloc ou les octets de la plage. Le carré du suivi et le fond dans **Mémoire** ont la même couleur. Chaque suivi utilise le cœur choisi lors de son ajout et colore la mémoire quand ce cœur est sélectionné. Les suivis sont conservés pendant la session et après un reset.

Dans **Suivis**, **↗** ouvre l’inspecteur ou la plage mémoire, **▤** ouvre l’adresse en mémoire et **−** retire le suivi sélectionné. Survolez les boutons pour afficher leur nom. Sélectionnez la ligne du groupe pour retirer une plage ou un bloc complet.

### Expressions

**Ctrl+E** ajoute une expression liée au cœur choisi. Par exemple, `r0 + 4`, `read32(counter)` et `*sp` affichent une valeur au fil des actualisations. `read8`, `read16` et `read32` lisent respectivement un octet, un demi-mot et un mot.

### Arrêts

**F9** ajoute un point d’arrêt sur une adresse, un registre ou un symbole. **+ Accès** ajoute un watchpoint que la cible déclenche lors d’un accès CPU à une plage, selon le mode choisi. La carte réelle limite le nombre et l’alignement des watchpoints. Les suivis classiques affichent les valeurs pendant l’inspection.

## Assembleur

**Ctrl+1** affiche le panneau **Assembleur** au centre quand le débogueur est utilisé seul. Ouvrir l’IDE libère cet emplacement. Fermer l’IDE rétablit la visibilité précédente du panneau. **PC** suit l’instruction du cœur choisi. Saisissez une autre adresse ou un symbole dans le champ puis cliquez sur **Aller** pour parcourir le code.

Un double-clic sur une instruction ajoute ou retire son point d’arrêt. La flèche marque le PC. Le turquoise signale l’exécution et le violet la ligne C sélectionnée lorsqu’une correspondance DWARF est disponible. Le clic droit ouvre **Colonnes**, où vous pouvez afficher les opcodes, les symboles et la colonne **Source C**. Un clic sur un lien de cette colonne révèle la ligne dans l’IDE relié si le projet correspondant est ouvert.

L’IDE affiche le C et le désassemblage complet de l’ELF à l’arrêt. Depuis le C, **Ctrl+Alt+A** révèle l’assembleur de la ligne sélectionnée. Le débogueur affiche son propre désassemblage en utilisation autonome.

Dans l’IDE, choisissez **Débogage → Quitter le débogage** pour revenir à l’édition. Fermer la fenêtre du débogueur a le même effet. L’IDE masque le volet **Assembleur**, retire les repères de débogage dans le C et garde les fichiers ouverts.

## Outils

Les raccourcis et **Vues → Outils** ouvrent les onglets suivants.

- La petite flèche au bas du débogueur replie **Outils**, puis restaure l’onglet actif et sa hauteur. Glissez le séparateur au-dessus du panneau pour le redimensionner. Le bouton **Console** reste accessible dans la barre d’exécution. **Ctrl+5** ouvre directement la Console. Les onglets d’Outils restent accessibles via **Vues → Outils**.
- **Carte mémoire** affiche les régions de la puce, leurs bornes, leurs droits et leur couverture. Double-cliquez sur une région pour ouvrir sa base dans Mémoire.
- **Temps virtuel** (**Ctrl+6**) fait avancer l’émulation sur une durée donnée. Choisissez entre exécuter le firmware et laisser avancer les périphériques avec les CPU figés.
- **Carte et câblage** (**Ctrl+7**) indique les connexions de la carte choisie et le port série ou les broches pertinentes.
- **Série TX/RX** (**Ctrl+Maj+U**) ouvre la liaison UART de la Raspberry Pi Debug Probe. Sélectionnez le port et le débit du firmware, puis cliquez sur **Connecter**. Le format est de 8 bits, sans parité, avec 1 bit d’arrêt. Les données RX s’affichent dans le journal. **Envoyer** transmet le texte saisi en UTF-8 sans fin de ligne automatique. Reliez les masses et croisez le TX et le RX entre la carte et la sonde. Le port reste ouvert quand vous changez d’onglet ou repliez Outils.

### Commandes de la console

Saisissez une commande puis validez avec **Entrée**. **help** rappelle les formats. Pour le pas à pas et les expressions, la console utilise le cœur choisi dans la barre d’exécution.

| Commande | Action |
| --- | --- |
| `backtrace` ou `bt` | Affiche la fonction courante et les appels du cœur sélectionné |
| `bt 0`, `bt 1`, `bt all` | Affiche la pile du cœur indiqué ou des deux cœurs |
| `break main` ou `break *0x10000200` | Ajoute un point d’arrêt à l’adresse ou au symbole |
| `watch counter` ou `watch 0x20000000, 1` | Arrête sur écriture dans la plage. La longueur par défaut est de 4 octets. `rwatch` cible les lectures, `awatch` les deux accès. |
| `delete breakpoints` ou `delete` | Supprime tous les points d’arrêt d’instruction |
| `delete main` ou `delete watchpoints` | Supprime le point à ce symbole ou tous les watchpoints |
| `c` / `continue` | Reprend l’exécution |
| `s` / `step` et `n` / `next` | Avance au pas en entrant dans les appels (s) ou en passant par-dessus (n). Sans ligne source, ces commandes travaillent au niveau instruction. |
| `print read32(counter)` ou `p r0` | Évalue une expression et affiche le résultat en hexadécimal et en décimal |


## Passer d’une vue à l’autre

| Depuis | Action | Résultat |
| --- | --- | --- |
| Périphériques | **Mémoire** | Ouvre la base du bloc avec ses offsets dans la grille |
| Périphériques | **Suivre le bloc** ou **Actions → Suivre le registre** | Ajoute une entrée et colore les adresses correspondantes dans Mémoire |
| Mémoire | **Actions → Suivre…** | Ajoute la valeur ou la plage sélectionnée dans Surveillance |
| Surveillance | **Mémoire** | Ouvre l’adresse du suivi et sélectionne son premier octet |
| Surveillance | **Ouvrir** sur un bloc ou un registre | Ouvre son inspecteur dans le contexte du suivi |
| Pile | Double-clic sur l’en-tête de trame | Ouvre l’inspecteur de cette trame et du cœur correspondant |
| Pile | Clic sur un octet, l’ASCII, un repère ou une variable | Met en évidence toute la plage dans les représentations de la trame |
| Carte mémoire | Double-clic sur une ligne | Ouvre la base de la région dans Mémoire |
| Assembleur | Clic sur **Source C** | Sélectionne la ligne dans le projet IDE correspondant |

### Comparer une variable SRAM et un périphérique

1. Arrêtez le firmware. Dans **Mémoire**, indiquez le symbole d’une variable globale, par exemple `counter` dans le projet bare metal, et cliquez sur **Lire**. Choisissez **Actions → Suivre…** et la largeur de la variable.
2. Dans **Périphériques**, sélectionnez le bloc à comparer, puis cliquez sur **Suivre le bloc**. Pour limiter le suivi à un registre, sélectionnez-le et utilisez **Actions → Suivre le registre**.
3. Dans **Surveillance → Suivis**, dépliez le bloc et comparez ses valeurs à la variable SRAM. Alternez entre leurs adresses avec **▤**. La grille reprend la couleur du suivi et le bandeau identifie le périphérique lorsque l’adresse est dans son espace MMIO.
4. Si vous voulez interrompre l’exécution quand le firmware écrit dans la variable, ajoutez séparément un arrêt sur accès avec `watch counter` dans la console. Après l’arrêt, `bt all` affiche les appels des deux cœurs disponibles. Double-cliquez sur une trame dans **Piles par cœur** pour consulter ses octets et ses valeurs décodées.
