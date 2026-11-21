cmake_minimum_required(VERSION 3.16)

# Projet autonome. Son compilateur cible le microcontrôleur ARM.
# debugger produit un ELF à chargement direct. board passe par le Pico SDK.
set(BOOT_BLINK_MODE "debugger" CACHE STRING "debugger (direct ELF) or board (SDK/UF2)")
set_property(CACHE BOOT_BLINK_MODE PROPERTY STRINGS debugger board)
set(PICO_BOARD "pico2" CACHE STRING "pico or pico2 (without Wi-Fi)")
set_property(CACHE PICO_BOARD PROPERTY STRINGS pico pico2)
set(BOOT_BLINK_HALF_PERIOD_MS "500" CACHE STRING "LED on/off duration in ms, 1..60000")
# Les cartes sans Wi-Fi partagent GPIO25. Vérifier aussi la plage de temporisation.
if(NOT PICO_BOARD MATCHES "^(pico|pico2)$")
    message(FATAL_ERROR "PICO_BOARD must be pico or pico2. Wireless boards use a different LED path.")
endif()
if(NOT BOOT_BLINK_HALF_PERIOD_MS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "BOOT_BLINK_HALF_PERIOD_MS must be an integer in 1..60000")
endif()
if(BOOT_BLINK_HALF_PERIOD_MS GREATER 60000)
    message(FATAL_ERROR "BOOT_BLINK_HALF_PERIOD_MS must be <= 60000")
endif()

if(BOOT_BLINK_MODE STREQUAL "board")
    # Le SDK est une dépendance locale. Vérifier son fichier d'import.
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}" CACHE PATH "Local Pico SDK (2.2.0 or newer)")
    if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
        message(FATAL_ERROR "Set PICO_SDK_PATH to a local Pico SDK checkout. See README.md.")
    endif()
    if(PICO_BOARD STREQUAL "pico2")
        # Démarrage ARM Secure sur RP2350. Le profil Pico choisit RP2040.
        set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "ARM Secure SDK platform")
        if(NOT PICO_PLATFORM STREQUAL "rp2350-arm-s")
            message(FATAL_ERROR "The Pico 2 board build uses PICO_PLATFORM=rp2350-arm-s")
        endif()
    else()
        set(PICO_PLATFORM "rp2040" CACHE STRING "RP2040 SDK platform")
        if(NOT PICO_PLATFORM STREQUAL "rp2040")
            message(FATAL_ERROR "The Pico board build uses PICO_PLATFORM=rp2040")
        endif()
    endif()
    # Importer la configuration du compilateur avant l'activation des langages.
    include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    project(BootBlink LANGUAGES C CXX ASM)
    pico_sdk_init()
    if(PICO_SDK_VERSION_STRING VERSION_LESS "2.2.0")
        message(FATAL_ERROR "This example requires Pico SDK >= 2.2.0; documented baseline: 2.2.0")
    endif()
elseif(BOOT_BLINK_MODE STREQUAL "debugger")
    # Le démarrage C minimal utilise directement la toolchain ARM bare metal.
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
    project(BootBlink LANGUAGES C)
else()
    message(FATAL_ERROR "BOOT_BLINK_MODE must be debugger or board")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # Options de compilation pour les outils d'analyse.
# Construire le même code applicatif dans les deux modes.
add_executable(boot_blink src/main.c)
set_target_properties(boot_blink PROPERTIES SUFFIX ".elf" INTERPROCEDURAL_OPTIMIZATION FALSE)
target_include_directories(boot_blink PRIVATE include startup)
target_compile_definitions(boot_blink PRIVATE BOOT_BLINK_HALF_PERIOD_MS=${BOOT_BLINK_HALF_PERIOD_MS})
# Avertissements C. Symboles et code peu transformé pour les points d'arrêt.
target_compile_options(boot_blink PRIVATE -Wall -Wextra -Wshadow -Wconversion -O0 -g3)

if(BOOT_BLINK_MODE STREQUAL "board")
    # Le SDK fournit le runtime. Ce module utilise ses API GPIO et de temps.
    target_sources(boot_blink PRIVATE src/platform_sdk.c)
    target_link_libraries(boot_blink PRIVATE pico_stdlib)
    # Ce clignotement n'utilise ni console USB ni console UART.
    pico_enable_stdio_usb(boot_blink 0)
    pico_enable_stdio_uart(boot_blink 0)
    pico_set_program_name(boot_blink "boot_blink")
    pico_set_program_version(boot_blink "1.0")
    pico_add_extra_outputs(boot_blink) # Produit entre autres la copie UF2 pour BOOTSEL.
    message(STATUS "Board image: SDK startup, physical GPIO25, boot_blink.uf2 for ${PICO_BOARD}")
else()
    # Capacités mémoire du script de lien et sélection des registres du profil.
    if(PICO_BOARD STREQUAL "pico2")
        set(BLINK_RAM_KIB 520)
        set(BLINK_FLASH_KIB 4096)
        set(BLINK_RP2350 1)
    else()
        set(BLINK_RAM_KIB 264)
        set(BLINK_FLASH_KIB 2048)
        set(BLINK_RP2350 0)
    endif()
    # Remplacer les marqueurs @...@. Écrire le script final dans le dossier de build.
    configure_file(startup/debugger.ld.in debugger.ld @ONLY)
    set(linker_script "${CMAKE_CURRENT_BINARY_DIR}/debugger.ld")
    target_sources(boot_blink PRIVATE src/platform_debugger.c startup/startup.c startup/memory_init.c)
    target_compile_definitions(boot_blink PRIVATE BOOT_BLINK_RP2350=${BLINK_RP2350})
    # Les deux variantes utilisent le sous-ensemble Thumb du Cortex-M0+.
    # Le Pico 2 sait l'exécuter. L'émulateur ne couvre qu'une partie de Thumb-2.
    # Le démarrage C autonome évite les fonctions automatiques de la libc.
    # Une section par fonction ou donnée aide le lien à éliminer le code inutilisé.
    # Retirer le déroulage de pile, le protecteur de pile et le code relogeable.
    target_compile_options(boot_blink PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
        -ffreestanding -fno-builtin -fno-common -ffunction-sections -fdata-sections
        -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-stack-protector -fno-pic -fno-pie)
    # Lier avec le démarrage du projet et sans les bibliothèques standard.
    # Le fichier .map détaille le placement obtenu. Le script impose les adresses.
    target_link_options(boot_blink PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
        -nostdlib -nostartfiles "-T${linker_script}" "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/boot_blink.map"
        -Wl,--gc-sections -Wl,--build-id=none)
    set_target_properties(boot_blink PROPERTIES LINK_DEPENDS "${linker_script}")
    message(STATUS "Debugger ELF: direct C startup, virtual SIO/timer; NOT a board boot image")
endif()
