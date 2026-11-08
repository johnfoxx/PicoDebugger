# Architecture et maintenance

## Frontières

`picodbg_core` est du C++11 sans Qt. `Memory` possède les octets et les périphériques ; chaque `Cpu` son contexte architectural. `Debugger` possède le bus, les deux CPU, l'ELF, les points d'arrêt et la politique d'exécution. Console et Qt utilisent cette même API.

Le contrôleur est non copiable et à propriétaire unique. Qt exécute des tranches courtes puis rafraîchit les panneaux sur son thread principal ; aucun thread concurrent ne lit un CPU en cours de mutation. Un futur worker devra publier des snapshots immuables des deux cœurs et des plages mémoire plutôt que partager les objets mutables.

## Invariants

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

## Correspondance source C++

`sourceHint(address)` fournit un symbole précédent + offset, pas une ligne C++. La GUI réserve une zone à côté de l'assembleur. La prochaine couche devra charger DWARF, gérer chemins de compilation et répertoires fournis par l'utilisateur, puis résoudre adresse → fichier/ligne/colonne.

Prévoir un `SourceResolver` indépendant de Qt. Le déroulage de pile devra utiliser DWARF CFI et les cadres d'exception, plutôt que deviner les retours en scannant des valeurs. La lecture des variables typées et le démanglage sont distincts de la lecture de mots mémoire.

## Validation

Effectuée : GCC C++11, suites CTest, console batch, ELF synthétique reproductible, AddressSanitizer/UBSan avec détection des fuites désactivée à cause de `ptrace`. Le parcours exhaustif du décodeur teste sa robustesse, pas la sémantique de chaque encodage.

Non effectuée : compilation/exécution Qt (paquets de développement absents), compilation croisée ARM (outil absent), comparaison au silicium et conformité ARM.

Sur une machine équipée de Qt : construire la GUI, ouvrir l'exemple, vérifier F10/F5/Maj+F5, breakpoint par double-clic, watch `read32(counter)`, lancement cœur 1, reset/changement de profil, agencement sur petit écran. L'inspection doit préserver FIFO/spinlocks.
