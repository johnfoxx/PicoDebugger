# Programmer une Pico avec l'IDE C

Lancer `pico-debugger` ou `pico-debugger /chemin/projet` pour commencer dans l'IDE. Créer, ouvrir et compiler un projet depuis cette fenêtre. Le bouton **Débogueur** ouvre la session à la demande. Pour commencer dans le débogueur, utiliser `pico-debugger --debugger` ou passer directement un ELF. **Fichier → Ouvrir l'IDE C** ou **Ctrl+Maj+E** l'ouvrent dans l'autre sens. Les deux fenêtres peuvent rester côte à côte ou sur deux écrans. Leurs barres d'état indiquent si l'autre fenêtre est reliée ou fermée et si l'émulateur ou la carte est en cours d'exécution.

## Premier projet

1. Dans **Projet → Nouveau projet…**, choisir un nom, un dossier parent, une carte et un modèle. **C autonome · émulateur et carte** fournit un compteur en C, un démarrage minimal et un script de lien en SRAM. Le même ELF ARM compilé sans SDK se lance dans l'émulateur et sur la carte via SWD en mode RAM. **Projet C avec Pico SDK** utilise le SDK et fait clignoter la LED du profil sélectionné. Sur les cartes W, cet exemple utilise le composant externe CYW43.
2. Modifier `src/main.c` dans l'onglet ouvert. L'arborescence permet d'ouvrir, de renommer ou de mettre à la corbeille les fichiers et de créer des fichiers ou dossiers dans le projet. Un fichier modifié est marqué d'un point dans l'onglet. **Ctrl+S** enregistre le fichier et **Ctrl+Maj+S** enregistre tous les fichiers ouverts. Les modèles ajoutent les fichiers `.c` de `src/` à la compilation. Dans un projet importé, suivre les règles de son CMake.
3. Ouvrir **Compilation → Outils du projet…** si un outil est introuvable. Les champs vides détectent les exécutables dans le `PATH`, et le SDK à partir de `PICO_SDK_PATH` ou d'un dossier usuel. Chaque chemin peut être réglé manuellement. La même boîte de dialogue présente les liens d'installation.
4. Utiliser **Configurer CMake** pour découvrir les exécutables et produire la base d'options du compilateur. **Ctrl+B** enregistre les sources, configure puis compile. La sortie est visible pendant le travail et la compilation peut être annulée. Choisir **Toutes les cibles** ou un exécutable CMake dans la barre d'outils.

**Projet → Ouvrir un projet…** importe un dossier contenant déjà `CMakeLists.txt`. Sélectionner la carte avant de configurer et vérifier que le CMake importé accepte `PICO_BOARD`. L'IDE construit en mode Debug dans `.picodbg/build/`, avec un dossier distinct pour chaque combinaison de carte et de chemins d'outils. Les projets créés contiennent leurs valeurs initiales dans `.picodbg-project.json`. Les choix locaux de carte et de cible sont mémorisés dans les réglages. Un projet CMake qui requiert des options particulières peut les déclarer dans son propre CMake. Les préréglages CMake personnalisés ne sont pas interprétés par l'IDE.

Les fichiers générés sont commentés en français. Pour suivre le démarrage autonome, partir de `CMakeLists.txt` et de `cmake/arm-none-eabi.cmake`, puis lire `startup/ram.ld`, `startup/startup.c` et `startup/memory_init.c` avant `src/main.c`. Pour voir le source commenté au milieu des instructions d'un ELF compilé avec les informations de débogage, utiliser `arm-none-eabi-objdump -d -S firmware.elf`. Les deux générateurs Python du dossier `examples/` décrivent leurs opcodes Thumb à côté des valeurs encodées.

## Écrire et contrôler le C

L'éditeur affiche les numéros de ligne, colore la syntaxe et reprend l'indentation. Il ouvre les fichiers texte UTF-8 du projet, dont les en-têtes et les scripts CMake. Les commandes **Annuler**, **Rétablir** et **Rechercher** s'appliquent à l'onglet courant.

Lorsque clangd tourne, les diagnostics apparaissent pendant la saisie et **Ctrl+Espace** affiche les suggestions au curseur. Pour résoudre les inclusions et macros du projet, configurer ou compiler au moins une fois afin que CMake produise `compile_commands.json`. Le programme déclare précisément le chemin du compilateur ARM autorisé pour l'interrogation des en-têtes système. Les modèles fournissent une configuration `.clang-tidy` avec des contrôles d'analyse statique et d'erreurs fréquentes. Les projets importés utilisent leurs propres réglages clangd et clang-tidy.

L'onglet **Problèmes** rassemble les diagnostics de clangd et les messages du compilateur localisés dans les fichiers du projet. Double-cliquer sur une ligne pour atteindre la source. La sortie complète de CMake et du compilateur reste dans **Compilation**. L'analyse statique couvre certaines classes d'erreurs et les tests du firmware restent nécessaires pour valider son comportement. Sans clangd, l'éditeur conserve la coloration et les diagnostics de compilation.

## Lancer le firmware

| Action | Résultat |
|---|---|
| **F5** ou **Ctrl+F5** dans l'IDE | Compile et charge l'ELF ARM à l'arrêt dans l'émulateur. |
| **Téléverser et déboguer sur la carte…** | Compile, demande le mode Flash ou RAM, charge les symboles et téléverse. La carte reste arrêtée. |
| **F5** dans le débogueur | Continue l'exécution jusqu'au prochain arrêt ou à la demande de pause. |
| **Maj+F5** | Demande l'arrêt de l'exécution dans la session partagée. En SWD, l'état « arrêtée » apparaît après confirmation par la carte. |
| **F10** | Avance d'une instruction du cœur sélectionné dans la session partagée. |

Pour le lancement SWD, ouvrir le débogueur, choisir le même profil de carte, connecter la sonde et arrêter la cible. L'IDE affiche l'état de cette session. Un échec de compilation ou de transfert empêche le lancement. Un ELF entièrement lié en SRAM avec une table de vecteurs présélectionne **RAM**. La Flash requiert une image amorçable conçue pour la carte. Après un reset matériel, retéléverser l'ELF RAM pour relancer le projet autonome. Les anciens projets générés avec le modèle d'émulation v0.7.0 gardent leur script de lien d'origine. [Câblage et conditions du téléversement](HARDWARE_DEBUG.md)

Si la sonde est connectée au moment de lancer l'émulation, le débogueur arrête et déconnecte la carte avant de charger l'ELF dans le simulateur.

La portée de l'émulation dépend du firmware. Les interfaces de projet, de serveur de langage, d'éditeur et de lancement sont distinctes pour permettre d'ajouter des langages et leurs outils de compilation par la suite.

## Suivre le C et l'assembleur

Compiler avec les lignes DWARF dans l'ELF (`-g`, inclus dans les modèles Debug). Quand la session est à l'arrêt, un volet redimensionnable affiche l'assembleur à droite du C dans l'IDE. GNU Binutils lit directement l'ELF. L'éditeur reprend toute la largeur du volet pendant l'exécution continue. Pour garder cette disposition à l'arrêt, décocher **Débogage → Afficher l'assembleur à l'arrêt**.

- **Turquoise** marque les instructions de la ligne C du PC, avec une flèche en marge de l'éditeur et sur l'instruction exacte. **Débogage → Suivre le PC dans le C** commande le défilement automatique. En émulation, l'éditeur échantillonne les tranches d'exécution. Via SWD, le PC est actualisé aux arrêts.
- **Violet** marque les instructions d'une autre ligne C dès qu'on y place le curseur. L'IDE affiche aussi les plages disjointes qu'un compilateur peut attribuer à une même ligne. **Ctrl+Alt+A** place la vue assembleur du débogueur à la première adresse de la sélection. Cocher **Suivre PC** dans le débogueur pour revenir au PC.
- Dans le débogueur, cliquer une cellule de la colonne **Source C** affiche la ligne dans l'aperçu et dans l'IDE si le fichier appartient au projet. Un double-clic dans les autres colonnes pose ou retire un point d'arrêt. Décocher **Vues → Afficher l'assembleur dans le débogueur** pour libérer le centre au profit des périphériques. **Ctrl+1** rétablit le tableau.

Les adresses et les lignes dépendent des tables produites par le compilateur. Une ligne peut correspondre à plusieurs plages, et l'optimisation peut fusionner ou éliminer des instructions. Un fichier modifié dans l'éditeur doit être recompilé pour retrouver une correspondance fiable. Les fichiers doivent être accessibles aux chemins enregistrés lors de la compilation. F10 reste un pas instruction. [Lecture des lignes DWARF dans Binutils](https://sourceware.org/binutils/docs/binutils/objdump.html)

## Gestion de configuration Git

**Projet → Git…** utilise l'exécutable Git détecté ou celui configuré dans **Outils du projet**. Le panneau ouvre le dépôt qui contient le projet et présente la branche et les changements du dépôt. Pour un projet autonome, **Initialiser** crée son dépôt sur demande.

Sélectionner les fichiers pour les indexer ou les désindexer, consulter leurs diffs, puis créer un commit avec un message. **Historique** liste les derniers commits. **Distant…** configure `origin`. **Récupérer** exécute un fetch et **Pousser** envoie la branche courante vers `origin`. L'authentification suit la configuration Git existante, y compris le gestionnaire d'identifiants ou les clés SSH. [Documentation Git](https://git-scm.com/docs)

## Installer les dépendances dans l'espace de PicoDebugger

Dans **Compilation → Installer les dépendances localement…**, sélectionner les paquets et cliquer **Installer la sélection**. Ils sont installés dans le sous-dossier `toolchains` des données utilisateur de PicoDebugger. L'installation n'utilise pas de droits administrateur et ne modifie ni le `PATH` global ni les fichiers de démarrage du shell. Les chemins configurés manuellement ont priorité, puis viennent les installations locales et la détection système.

Le catalogue fourni est [config/dependencies.json](../config/dependencies.json). L'application en crée une copie utilisateur dans son dossier de configuration. **Ouvrir le catalogue…** ouvre cette copie. Elle définit les plateformes, les URL HTTPS, les versions, les empreintes SHA-256 des archives et le commit du SDK. Pour une mise à jour, modifier l'URL, la version et l'empreinte ou le commit ensemble, puis rouvrir l'installateur. Les autres versions installées restent disponibles sur disque.

Le catalogue fourni couvre Linux x86-64 et Linux AArch64 pour les paquets Arm et Raspberry Pi, CMake et Ninja. Le binaire clangd fourni est celui de Linux x86-64. L'installation passe par Python 3.9 ou plus récent, avec le filtre `tarfile.data_filter` pour les archives TAR. Le SDK est récupéré avec Git et ses sous-modules. L'installation locale ne règle pas les autorisations USB ni les bibliothèques système éventuellement requises par les exécutables.

## Composants et mises à jour

| Composant | Rôle et mise à jour |
|---|---|
| Qt Widgets 5.15 ou 6 | Interface, éditeur `QPlainTextEdit`, coloration via `QSyntaxHighlighter`, modèle de fichiers et complétion. Mettre à jour les paquets de développement Qt et reconfigurer PicoDebugger, avec `PICODBG_QT_MAJOR` si nécessaire. [Qt](https://doc.qt.io/) |
| CMake 3.16 ou supérieur | Configuration, compilation et lecture des cibles et artefacts via la File API codemodel v2. Mettre à jour avec le gestionnaire de paquets ou choisir un nouvel exécutable dans **Outils du projet**. Installer également le constructeur utilisé par le générateur CMake, par exemple Make. [CMake](https://cmake.org/download/) · [File API](https://cmake.org/cmake/help/latest/manual/cmake-file-api.7.html) |
| Arm GNU Toolchain (`arm-none-eabi-gcc`) | Compilation ARM hors système pour les quatre cartes. Installer ou mettre à niveau la chaîne bare-metal via les paquets de la distribution ou l'éditeur. Renseigner le nouveau compilateur si son emplacement change. [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) |
| GNU Binutils (`objdump`, `addr2line`) | Index adresse ↔ ligne C des ELF avec DWARF. PicoDebugger cherche les binaires ARM à côté du compilateur, puis dans le `PATH`. Les mettre à jour avec la chaîne ARM. [GNU Binutils](https://sourceware.org/binutils/docs/binutils.html) |
| Pico SDK | Bibliothèques et démarrage des firmwares destinés à une carte. Préférer une version publiée compatible avec le profil choisi, avec ses sous-modules, puis mettre à jour `PICO_SDK_PATH` ou le champ de l'IDE. Les profils Pico 2 et Pico 2 W nécessitent une version du SDK qui les prend en charge. [Pico SDK et procédure officielle](https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html) · [Versions du SDK](https://github.com/raspberrypi/pico-sdk/releases) |
| clangd avec clang-tidy intégré | Diagnostics, contrôle statique et autocomplétion via LSP. Mettre à jour le paquet LLVM/clangd, puis sélectionner le nouvel exécutable et reconfigurer le projet. Les règles d'analyse sont pilotées par `.clang-tidy`. [Installation clangd](https://clangd.llvm.org/installation) · [Fonctionnalités](https://clangd.llvm.org/features) · [Protocole LSP](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/) |
| GDB ARM, OpenOCD et Debug Probe | Protocole matériel partagé avec le débogueur. Mettre à jour GDB et OpenOCD via les paquets ou versions officielles, avec les scripts associés à OpenOCD. Revoir ensuite les chemins dans la configuration de la sonde. [Guide officiel Debug Probe](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html) |

Les chemins d'outils sont détectés à l'ouverture de l'IDE et après enregistrement de **Outils du projet**. Après une mise à jour sur place du compilateur ou du SDK, fermer le projet, écarter l'ancien dossier de compilation sous `.picodbg/build/`, puis reconfigurer pour reconstruire le cache. Conserver les sources et la configuration à la racine.
