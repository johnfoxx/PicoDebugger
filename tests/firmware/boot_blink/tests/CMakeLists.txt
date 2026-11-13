cmake_minimum_required(VERSION 3.16)
project(BootBlinkHostTests LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

add_library(blink_application OBJECT ../src/main.c)
target_include_directories(blink_application PRIVATE ../include)
target_compile_definitions(blink_application PRIVATE main=firmware_main BOOT_BLINK_HALF_PERIOD_MS=1)
target_compile_options(blink_application PRIVATE -Wall -Wextra -Werror -O0)

add_executable(boot_blink_host_tests test_host.c ../startup/memory_init.c
    $<TARGET_OBJECTS:blink_application>)
target_include_directories(boot_blink_host_tests PRIVATE ../include ../startup)
target_compile_options(boot_blink_host_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME startup_memory COMMAND boot_blink_host_tests memory)
add_test(NAME blink_sequence COMMAND boot_blink_host_tests blink)
set_tests_properties(startup_memory blink_sequence PROPERTIES TIMEOUT 5)
