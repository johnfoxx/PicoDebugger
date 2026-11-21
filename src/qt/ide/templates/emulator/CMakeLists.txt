cmake_minimum_required(VERSION 3.16)

set(PICO_BOARD "@BOARD_NAME@" CACHE STRING "Pico board")
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
project(@PROJECT_NAME@ LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(PICO_BOARD MATCHES "^pico2")
    set(BLINK_RAM_KIB 520)
    set(BLINK_FLASH_KIB 4096)
    set(BLINK_RP2350 1)
else()
    set(BLINK_RAM_KIB 264)
    set(BLINK_FLASH_KIB 2048)
    set(BLINK_RP2350 0)
endif()
configure_file(startup/debugger.ld.in debugger.ld @ONLY)
set(linker "${CMAKE_CURRENT_BINARY_DIR}/debugger.ld")

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "src/*.c")
add_executable(@PROJECT_NAME@ ${sources} startup/startup.c startup/memory_init.c)
set_target_properties(@PROJECT_NAME@ PROPERTIES SUFFIX ".elf" LINK_DEPENDS "${linker}")
target_include_directories(@PROJECT_NAME@ PRIVATE include startup)
target_compile_definitions(@PROJECT_NAME@ PRIVATE BOOT_BLINK_HALF_PERIOD_MS=500 BOOT_BLINK_RP2350=${BLINK_RP2350})
target_compile_options(@PROJECT_NAME@ PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
    -Wall -Wextra -Wshadow -Wconversion -O0 -g3 -ffreestanding -fno-builtin
    -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables
    -fno-stack-protector -fno-pic -fno-pie)
target_link_options(@PROJECT_NAME@ PRIVATE -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
    -nostdlib -nostartfiles "-T${linker}" -Wl,--gc-sections -Wl,--build-id=none)
