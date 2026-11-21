# La cible est un microcontrôleur ARM sans système d'exploitation.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
# Tester le compilateur avec une bibliothèque statique.
# L'exécutable final aura besoin du startup et du script de lien du projet.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Chercher GCC pour ARM bare metal dans le PATH configuré pour la compilation.
find_program(BOOT_BLINK_ARM_GCC arm-none-eabi-gcc)
if(NOT BOOT_BLINK_ARM_GCC)
    message(FATAL_ERROR
        "arm-none-eabi-gcc absent. Install gcc-arm-none-eabi or add its bin directory to PATH. "
        "See tests/firmware/boot_blink/README.md; this ELF cannot be built with the host compiler.")
endif()
# Utiliser ce compilateur pour tous les fichiers C de la cible.
set(CMAKE_C_COMPILER "${BOOT_BLINK_ARM_GCC}")
