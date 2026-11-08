# Versions

## Après v0.1.0 — extension périphériques

- Explorateur des blocs RP2040/RP2350 : filtre, registres/champs connus et couverture explicite ; fenêtre détachable `Ctrl+P`.
- Timer RP2040 séparé du bus : compteur 64 bits, quatre alarmes, interruptions, pause et alias RW. Temps virtuel, sans horloges/watchdog matériels.
- Routage des IRQ timer vers les deux cœurs et progression pendant WFI ; suivi des interruptions externes actives pour éviter un redéclenchement artificiel pendant le handler.
- Cartographie RP2350 détaillée, registres CPU lisibles en hexadécimal complet, contraste des docks détachés corrigé.
- Références officielles et procédure de récupération avec manifeste SHA-256 ; couverture et trajectoire de conformité documentées.
- 11 tests CTest avec Qt5 ; suites moteur sous ASan/UBSan (détection des fuites désactivée).

Cette extension ne constitue pas une émulation complète. Le tag `v0.1.0` reste attaché au jalon précédent.

## v0.1.0 — 2026-11-08

Premier jalon graphique : moteur C++11 ARM/Thumb partiel, ELF32, deux contextes CPU, pas/reset/breakpoints/watchpoints, inspection simultanée CPU/mémoire/pile/assembleur et périphériques modélisés. Interface Qt5 compilée ; 10 tests CTest réussis, y compris les interactions GUI offscreen.

Ce tag préserve la base avant l'extension des périphériques. La simulation complète des RP2040/RP2350, les programmes Pico SDK arbitraires et l'équivalence avec la cible physique ne sont pas acquis ; voir COVERAGE.md. Le tag est local, aucun dépôt distant n'est configuré.
