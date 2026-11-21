cmake_minimum_required(VERSION 3.16)

# PicoDebugger fournit le profil de carte. Les profils partagent ce build ARM.
set(PICO_BOARD "@BOARD_NAME@" CACHE STRING "Target board")
# CMake doit connaître le compilateur croisé avant d'activer le langage C.
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
project(@PROJECT_NAME@ LANGUAGES C)
# Exiger C11 et utiliser le mode C standard du compilateur.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Rechercher à nouveau les fichiers C si le contenu de src/ change.
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.c")
# Ajouter le code de démarrage à l'application.
add_executable(@PROJECT_NAME@ ${sources} startup/startup.c startup/memory_init.c)
# L'ELF décrit une image en SRAM. Relancer le lien si le script change.
set(linker "${CMAKE_CURRENT_SOURCE_DIR}/startup/ram.ld")
set_target_properties(@PROJECT_NAME@ PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${linker}")
target_include_directories(@PROJECT_NAME@ PRIVATE include startup)

# -mcpu et -mthumb choisissent les instructions Thumb du Cortex-M0+.
# Le Cortex-M33 du RP2350 exécute aussi ce sous-ensemble.
# -mfloat-abi=soft évite de dépendre d'une unité de calcul flottant.
# -Wall et -Wextra activent les avertissements. -O0 et -g3 facilitent le pas à pas.
# -ffreestanding et -fno-builtin écartent les hypothèses d'une application hébergée.
# -fno-common place les globales sans initialiseur dans .bss.
# Les sections par fonction et par donnée permettent d'éliminer le code inutilisé.
# Les options unwind retirent les tables de déroulage de pile.
# Les trois dernières options évitent les routines de protection de pile et le code relogeable.
target_compile_options(@PROJECT_NAME@ PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
    -Wall -Wextra -O0 -g3 -ffreestanding -fno-builtin -fno-common
    -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
    -fno-stack-protector -fno-pic -fno-pie)

# Au lien, reprendre la même cible ARM et la même convention d'appel.
# Le projet fournit son propre démarrage. Le lien exclut les bibliothèques standard.
# -T charge le plan mémoire. --gc-sections enlève les sections inutilisées.
# --build-id=none omet la note d'identification du binaire.
target_link_options(@PROJECT_NAME@ PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
    -nostdlib -nostartfiles "-T${linker}" -Wl,--gc-sections -Wl,--build-id=none)
