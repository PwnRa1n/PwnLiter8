cmake_minimum_required(VERSION 3.13)

# ====================================================================================

include(pico_sdk_import.cmake)

project(Pwnliter8 C CXX ASM)

set(CMAKE_BUILD_TYPE MinSizeRel)

SET(BOARD_DEFINITION ${CMAKE_SOURCE_DIR}/boards/${PICO_BOARD}.cmake)

if (NOT EXISTS ${BOARD_DEFINITION})
    SET(BOARD_DEFINITION ${CMAKE_SOURCE_DIR}/boards/unknown.cmake)
endif()

include(${BOARD_DEFINITION} OPTIONAL)

if (NOT DEFINED PWNLITER8_ENABLE_SHELL)
    add_compile_definitions(
        WITH_AUTO_MODE=1
    )
endif()

pico_sdk_init()

add_executable(Pwnliter8
    src/main.c
    src/led.c
    src/bus.c
    src/usb.c
    src/exploit.c
    src/exploit_identify.c
    src/exploit_transfer.c
    src/exploit_t8020_t8006.c
    src/exploit_t8030.c
)

pico_set_program_name(Pwnliter8 "Pwnliter8")
pico_set_program_version(Pwnliter8 "1.0")

target_include_directories(Pwnliter8 PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_compile_definitions(Pwnliter8 PRIVATE
    PICO_STACK_SIZE=0x1000
    PICO_PRINTF_SUPPORT_EXPONENTIAL=0
    PICO_PRINTF_SUPPORT_FLOAT=0
    PICO_PRINTF_SUPPORT_PTRDIFF_T=0
)

pico_enable_stdio_uart(Pwnliter8 0)
pico_enable_stdio_usb(Pwnliter8 1)

add_subdirectory(pio_usb)

target_link_libraries(Pwnliter8
    pico_stdlib
    pico_status_led
    hardware_pwm
    pio_usb
)

pico_add_extra_outputs(Pwnliter8)
