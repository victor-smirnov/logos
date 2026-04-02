cmake_minimum_required(VERSION 3.28)

# Set vcpkg toolchain path before project()
set(CMAKE_TOOLCHAIN_FILE "/home/victor/cxx/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")

project(logos LANGUAGES CXX C)

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

# Tools
add_subdirectory(tools/peg_gen)
