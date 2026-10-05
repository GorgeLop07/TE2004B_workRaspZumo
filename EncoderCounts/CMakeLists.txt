# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

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
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(EncoderCounts C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Pololu Zumo 2040 Robot library (motors, buttons, display, ...)
add_subdirectory(../zumo-2040-robot/c/pololu_zumo_2040_robot pololu_zumo_2040_robot)

# Add executable. Default name is the project name, version 0.1

add_executable(EncoderCounts EncoderCounts.c encoder.c)

pico_set_program_name(EncoderCounts "EncoderCounts")
pico_set_program_version(EncoderCounts "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(EncoderCounts 0)
pico_enable_stdio_usb(EncoderCounts 1)

# Add the standard library to the build
target_link_libraries(EncoderCounts
        pico_stdlib
        hardware_gpio
        hardware_sync
        pololu_zumo_2040_robot)

# Add the standard include files to the build
target_include_directories(EncoderCounts PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(EncoderCounts)

