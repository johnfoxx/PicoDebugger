set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(BOOT_BLINK_ARM_GCC arm-none-eabi-gcc)
if(NOT BOOT_BLINK_ARM_GCC)
    message(FATAL_ERROR
        "arm-none-eabi-gcc absent. Install gcc-arm-none-eabi or add its bin directory to PATH. "
        "See tests/firmware/boot_blink/README.md; this ELF cannot be built with the host compiler.")
endif()
set(CMAKE_C_COMPILER "${BOOT_BLINK_ARM_GCC}")
