# Programmer une Pico avec l'IDE C

Dans le débogueur, choisir **Fichier → Ouvrir l'IDE C** ou **Ctrl+Maj+E**. Le lancement `pico-debugger --ide` ouvre directement l'éditeur. Le bouton **Afficher le débogueur** affiche la même session. Les deux fenêtres peuvent rester côte à côte ou sur deux écrans. L'IDE indique l'état de la cible du débogueur dans sa barre d'état.

## Premier projet

1. Dans **Projet → Nouveau projet…**, choisir un nom, un dossier parent, une carte et un modèle. **Exemple C pour émulation** utilise le compilateur ARM sans Pico SDK et permet d'observer une sortie GPIO25 simulée. **Firmware Pico SDK pour carte réelle** utilise le SDK et fait clignoter la LED du profil sélectionné. Le modèle W utilise CYW43.
2. Modifier `src/main.c` dans l'onglet ouvert. L'arborescence permet d'ouvrir, de renommer ou de mettre à la corbeille les fichiers et de créer des fichiers ou dossiers dans le projet. Un fichier modifié est marqué d'un point dans l'onglet. **Ctrl+S** enregistre le fichier et **Ctrl+Maj+S** enregistre tous les fichiers ouverts. Les modèles ajoutent les fichiers `.c` de `src/` à la compilation. Dans un projet importé, suivre les règles de son CMake.
3. Ouvrir **Compilation → Outils du projet…** si un outil est introuvable. Les champs vides détectent les exécutables dans le `PATH`, et le SDK à partir de `PICO_SDK_PATH` ou d'un dossier usuel. Chaque chemin peut être réglé manuellement. La même boîte de dialogue présente les liens d'installation.
4. Utiliser **Configurer CMake** pour découvrir les exécutables et produire la base d'options du compilateur. **Ctrl+B** enregistre les sources, configure puis compile. La sortie est visible pendant le travail et la compilation peut être annulée. Choisir **Toutes les cibles** ou un exécutable CMake dans la barre d'outils.

**Projet → Ouvrir un projet…** importe un dossier contenant déjà `CMakeLists.txt`. Sélectionner la carte avant de configurer et vérifier que le CMake importé accepte `PICO_BOARD`. L'IDE construit en mode Debug dans `.picodbg/build/`, avec un dossier distinct pour chaque combinaison de carte et de chemins d'outils. Les projets créés contiennent leurs valeurs initiales dans `.picodbg-project.json`. Les choix locaux de carte et de cible sont mémorisés dans les réglages. Un projet CMake qui requiert des options particulières peut les déclarer dans son propre CMake. Les préréglages CMake personnalisés ne sont pas interprétés par l'IDE.

## Écrire et contrôler le C

L'éditeur affiche les numéros de ligne, colore la syntaxe et reprend l'indentation. Il ouvre les fichiers texte UTF-8 du projet, dont les en-têtes et les scripts CMake. Les commandes **Annuler**, **Rétablir** et **Rechercher** s'appliquent à l'onglet courant.

Lorsque clangd tourne, les diagnostics apparaissent pendant la saisie et **Ctrl+Espace** affiche les suggestions au curseur. Pour résoudre les inclusions et macros du projet, configurer ou compiler au moins une fois afin que CMake produise `compile_commands.json`. Le programme déclare précisément le chemin du compilateur ARM autorisé pour l'interrogation des en-têtes système. Les modèles fournissent une configuration `.clang-tidy` avec des contrôles d'analyse statique et d'erreurs fréquentes. Les projets importés utilisent leurs propres réglages clangd et clang-tidy.

L'onglet **Problèmes** rassemble les diagnostics de clangd et les messages du compilateur localisés dans les fichiers du projet. Double-cliquer sur une ligne pour atteindre la source. La sortie complète de CMake et du compilateur reste dans **Compilation**. L'analyse statique couvre certaines classes d'erreurs et les tests du firmware restent nécessaires pour valider son comportement. Sans clangd, l'éditeur conserve la coloration et les diagnostics de compilation.

## Lancer le firmware

| Action | Résultat |
|---|---|
| **Ctrl+F5** | Compile et ouvre l'ELF ARM dans le débogueur, à l'arrêt dans l'émulateur. |
| **F5** | Compile, ouvre l'ELF ARM dans l'émulateur et démarre la session. |
| **Lancer sur la carte…** | Compile pour la carte choisie, demande le mode Flash ou RAM, charge les symboles, téléverse et démarre après confirmation et réussite du transfert. |

Pour le lancement SWD, ouvrir le débogueur, choisir le même profil de carte, connecter la sonde et arrêter la cible. L'IDE affiche l'état de cette session. Un échec de compilation ou de transfert empêche le lancement. Un projet généré à partir de l'exemple d'émulation se lance dans le simulateur. Choisir un firmware Pico SDK amorçable pour la Flash ou un ELF RAM avec sa table de vecteurs pour le mode RAM. [Câblage et conditions du téléversement](HARDWARE_DEBUG.md)

Si la sonde est connectée au moment de lancer l'émulation, le débogueur arrête et déconnecte la carte avant de charger l'ELF dans le simulateur.

La portée de l'émulation dépend du firmware. Le débogueur conserve son pas instruction et les vues assembleur, symboles ELF et mémoire. L'IDE édite le C. Les interfaces de projet, de serveur de langage, d'éditeur et de lancement sont distinctes pour permettre d'ajouter des langages et leurs outils de compilation par la suite.

## Composants et mises à jour

| Composant | Rôle et mise à jour |
|---|---|
| Qt Widgets 5.15 ou 6 | Interface, éditeur `QPlainTextEdit`, coloration via `QSyntaxHighlighter`, modèle de fichiers et complétion. Mettre à jour les paquets de développement Qt et reconfigurer PicoDebugger, avec `PICODBG_QT_MAJOR` si nécessaire. [Qt](https://doc.qt.io/) |
| CMake 3.16 ou supérieur | Configuration, compilation et lecture des cibles et artefacts via la File API codemodel v2. Mettre à jour avec le gestionnaire de paquets ou choisir un nouvel exécutable dans **Outils du projet**. Installer également le constructeur utilisé par le générateur CMake, par exemple Make. [CMake](https://cmake.org/download/) · [File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html) |
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | Compilation ARM hors système pour les quatre cartes. Installer ou mettre à niveau la chaîne bare-metal via les paquets de la distribution ou l'éditeur. Renseigner le nouveau compilateur si son emplacement change. [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| Pico SDK | Bibliothèques et démarrage des firmwares destinés à une carte. Préférer une version publiée compatible avec le profil choisi, avec ses sous-modules, puis mettre à jour `PICO_SDK_PATH` ou le champ de l'IDE. Les profils Pico 2 et Pico 2 W nécessitent une version du SDK qui les prend en charge. [Pico SDK et procédure officielle](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html) · [Versions du SDK](https://github.com/raspberrypi/pico-sdk/releases) |
| clangd avec clang-tidy intégré | Diagnostics, contrôle statique et autocomplétion via LSP. Mettre à jour le paquet LLVM/clangd, puis sélectionner le nouvel exécutable et reconfigurer le projet. Les règles d'analyse sont pilotées par `.clang-tidy`. [Installation clangd](https://clangd.llvm.org/installation) · [Fonctionnalités](https://clangd.llvm.org/features) · [Protocole LSP](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/) |
| GDB ARM, OpenOCD et Debug Probe | Protocole matériel partagé avec le débogueur. Mettre à jour GDB et OpenOCD via les paquets ou versions officielles, avec les scripts associés à OpenOCD. Revoir ensuite les chemins dans la configuration de la sonde. [Guide officiel Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html) |

Les chemins d'outils sont détectés à l'ouverture de l'IDE et après enregistrement de **Outils du projet**. Après une mise à jour sur place du compilateur ou du SDK, fermer le projet, écarter l'ancien dossier de compilation sous `.picodbg/build/`, puis reconfigurer pour reconstruire le cache. Conserver les sources et la configuration à la racine.
