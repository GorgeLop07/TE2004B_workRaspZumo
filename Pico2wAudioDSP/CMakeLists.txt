cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(Pico2wAudioDSP C CXX ASM)

pico_sdk_init()

add_executable(Pico2wAudioDSP
    main.c
    audio_input.c
    mcp2515.c
    dsp/dsp_runtime.c
)

pico_set_program_name(Pico2wAudioDSP "Pico2wAudioDSP")
pico_set_program_version(Pico2wAudioDSP "0.1")

pico_enable_stdio_uart(Pico2wAudioDSP 0)
pico_enable_stdio_usb(Pico2wAudioDSP 1)

# Sin WiFi: este modulo no inicializa el chip CYW43 (lo necesita Pico2wLocalPositioningCAN).
target_link_libraries(Pico2wAudioDSP
    pico_stdlib
    hardware_spi
    hardware_adc
    hardware_dma
    )

target_include_directories(Pico2wAudioDSP PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/dsp
)

# Los filtros dependen de la FPU de simple precision del Cortex-M33.
target_compile_options(Pico2wAudioDSP PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)

pico_add_extra_outputs(Pico2wAudioDSP)
