cmake_minimum_required(VERSION 3.28)

# Jenny toolchain must be set before vcpkg (vcpkg chains it in).
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/toolchains/jenny.cmake"
    CACHE STRING "Jenny (custom Clang 21) compiler toolchain")

# vcpkg toolchain — includes VCPKG_CHAINLOAD_TOOLCHAIN_FILE internally.
set(CMAKE_TOOLCHAIN_FILE "/home/victor/cxx/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

project(logos LANGUAGES CXX C ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# Find dependencies
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# Global includes
include_directories(include)

# Modules
add_subdirectory(src/verification)
add_subdirectory(src/core)
add_subdirectory(src/hermes)
add_subdirectory(src/reactor)

add_subdirectory(src/hrpc)

# Compiler
add_subdirectory(src/compiler)

# Tools
add_subdirectory(tools/peg_gen)
add_subdirectory(tools/hrpc_gen)
add_subdirectory(tools/logos-audit)

# Tests
enable_testing()
add_subdirectory(tests/logos)
