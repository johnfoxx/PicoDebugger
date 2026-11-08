# PicoDebugger

Projet de débogueur spécialisé pour Raspberry Pi Pico, développé en C++ (C++11 minimum), dans un dépôt Git indépendant.

## Besoin

- Interface ergonomique : instructions assembleur, registres et indicateurs CPU, mémoire, pile et cartographie des périphériques.
- Inspection des deux cœurs et affichage explicite de la cohérence et de la fraîcheur des données.
- Chargement d'un exécutable, démarrage à l'arrêt, pas à pas, reprise, pause, reset et redémarrage.
- Points d'arrêt, points de surveillance mémoire, expressions surveillées et console d'évaluation.
- Diagnostic des exceptions et fautes du processeur.
- Préparation de l'association assembleur/source C++ pour une seconde itération.

## Choix à préciser

La cible prioritaire et le mode d'exécution (carte via SWD ou simulation sans carte) restent à confirmer. Une émulation partielle ne constitue pas une garantie de compatibilité avec le matériel réel. La couverture des CPU et périphériques, ainsi que les limites temporelles et multicœurs, devront être documentées et vérifiées.

## État

Initialisation du dépôt ; aucune implémentation du débogueur pour le moment.
