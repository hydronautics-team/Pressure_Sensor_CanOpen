cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Pressure_Sensor_SAUVC)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# CANoopEn is kept as an unmodified submodule.  Build the selected stack layers
# locally so the firmware controls the feature set and the generated OD.
file(GLOB CANOOPEN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Common/*.cpp"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/ObjectDictionary/*.cpp"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Protocols/*.cpp"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Slave/*.cpp"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/System/*.cpp"
)

add_library(CANoopEn STATIC ${CANOOPEN_SOURCES})
target_include_directories(CANoopEn PUBLIC
    # The reduced project settings intentionally override the stack defaults.
    "${CMAKE_SOURCE_DIR}/Core/Inc/CANopen"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Common"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/ObjectDictionary"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Protocols"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Slave"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/Settings"
    "${CMAKE_SOURCE_DIR}/Drivers/CANoopEn/Source/System"
)
target_compile_definitions(CANoopEn PUBLIC
    CO_ENABLE_MASTER=0
    CO_ENABLE_SLAVE=1
    CO_ENABLE_HEARTBEAT=1
    CO_ENABLE_SYNC=0
    CO_ENABLE_TIME=0
    CO_ENABLE_EMCY=0
    CO_ENABLE_PDO=1
    CO_ENABLE_MPDO=0
    CO_ENABLE_SDO_SERVER=1
    CO_ENABLE_SDO_CLIENT=0
    CO_ENABLE_SDO_BLOCK_TRANSFER=0
    CO_ENABLE_LSS=0
    CO_ENABLE_INDICATORS=0
)
target_compile_options(CANoopEn PRIVATE -fno-use-cxa-atexit)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    CANoopEn

    # Add user defined libraries
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

# Static analysis for application C++ sources. The helper discovers the
# arm-none-eabi standard-library include paths used by the active toolchain.
add_custom_target(clang-tidy
    COMMAND ${CMAKE_COMMAND} -E env
        "ARM_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
        bash "${CMAKE_SOURCE_DIR}/tools/run-clang-tidy.sh" "${CMAKE_BINARY_DIR}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running clang-tidy for application sources"
    USES_TERMINAL
    VERBATIM
)
