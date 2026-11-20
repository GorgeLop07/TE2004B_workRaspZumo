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

project(ZumoRouteFollower C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Initialize the official Pololu repository as a submodule at
# ./zumo-2040-robot, or point this cache variable at an existing checkout.
set(ZUMO_ROBOT_LIBRARY_DIR
    "${CMAKE_CURRENT_LIST_DIR}/zumo-2040-robot/c/pololu_zumo_2040_robot"
    CACHE PATH "Path to the Pololu Zumo 2040 C library")
if (NOT EXISTS "${ZUMO_ROBOT_LIBRARY_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "Pololu Zumo 2040 C library not found at '${ZUMO_ROBOT_LIBRARY_DIR}'. "
        "Initialize the zumo-2040-robot submodule or set ZUMO_ROBOT_LIBRARY_DIR.")
endif()
add_subdirectory("${ZUMO_ROBOT_LIBRARY_DIR}" pololu_zumo_2040_robot)

# Add executable. Default name is the project name, version 0.1

add_executable(ZumoRouteFollower route_follower.c mcp2515.c encoders.c)
pico_generate_pio_header(ZumoRouteFollower
        ${CMAKE_CURRENT_LIST_DIR}/quadrature_encoder.pio)

pico_set_program_name(ZumoRouteFollower "ZumoRouteFollower")
pico_set_program_version(ZumoRouteFollower "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ZumoRouteFollower 0)
pico_enable_stdio_usb(ZumoRouteFollower 1)

# Add the standard library to the build
target_link_libraries(ZumoRouteFollower
        pico_stdlib)
target_link_libraries(ZumoRouteFollower
        hardware_gpio
        hardware_pio
        hardware_spi
        pololu_zumo_2040_robot)

# Add the standard include files to the build
target_include_directories(ZumoRouteFollower PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(ZumoRouteFollower)

