cmake_minimum_required(VERSION 3.16)

# Le profil du SDK sélectionne les broches et les composants de la carte.
set(PICO_BOARD "@BOARD_NAME@" CACHE STRING "Pico board")
if(PICO_BOARD MATCHES "^pico2")
    # Les deux profils Pico 2 utilisent le démarrage ARM Secure du SDK.
    set(PICO_PLATFORM "rp2350-arm-s" CACHE STRING "RP2350 architecture")
endif()

# Préférer le chemin fourni à CMake. Sinon, le lire dans l'environnement.
if(NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
endif()
if(NOT EXISTS "${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
    message(FATAL_ERROR "Set PICO_SDK_PATH to a Raspberry Pi Pico SDK checkout with its submodules.")
endif()

# Importer le SDK avant project() pour lui laisser choisir la toolchain.
# Ses bibliothèques et son démarrage utilisent aussi le C++ et l'assembleur.
include("${PICO_SDK_PATH}/external/pico_sdk_import.cmake")
project(@PROJECT_NAME@ LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
pico_sdk_init()

# Reconfigurer si un fichier .c est ajouté ou retiré de src/.
file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.c")
add_executable(@PROJECT_NAME@ ${sources})
target_include_directories(@PROJECT_NAME@ PRIVATE include)
# Appliquer les avertissements aux unités C seulement.
target_compile_options(@PROJECT_NAME@ PRIVATE "$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>")
# pico_stdlib apporte le démarrage du SDK, les API GPIO et la temporisation.
target_link_libraries(@PROJECT_NAME@ PRIVATE pico_stdlib)
if(PICO_CYW43_SUPPORTED)
    # Sur une carte W, le composant CYW43 pilote la LED sans pile réseau ici.
    target_link_libraries(@PROJECT_NAME@ PRIVATE pico_cyw43_arch_none)
endif()
# Produire notamment l'UF2 pour BOOTSEL et garder l'ELF pour le débogueur.
pico_add_extra_outputs(@PROJECT_NAME@)
