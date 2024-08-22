cmake_minimum_required(VERSION 3.10)

project(FlashLibrary)

# Specify the source files
set(SOURCES
    src/flash_lib.c
)

# Create the library
add_library(flash_lib STATIC ${SOURCES})

# Specify include directories
target_include_directories(flash_lib PUBLIC include)

# Link the necessary libraries
target_link_libraries(flash_lib
    pico_stdlib
    hardware_flash
)
