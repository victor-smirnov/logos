# cmake/toolchains/jenny.cmake
#
# Jenny toolchain: Victor Smirnov's Clang 21 fork.
# Loaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE so vcpkg integration still works.

set(JENNY_ROOT "/opt/jenny-21x" CACHE PATH "Jenny (Clang 21) installation root")

set(CMAKE_C_COMPILER   "${JENNY_ROOT}/bin/clang"   CACHE STRING "C compiler")
set(CMAKE_CXX_COMPILER "${JENNY_ROOT}/bin/clang++" CACHE STRING "C++ compiler")
set(CMAKE_AR           "${JENNY_ROOT}/bin/llvm-ar"     CACHE STRING "Archiver")
set(CMAKE_RANLIB       "${JENNY_ROOT}/bin/llvm-ranlib" CACHE STRING "Ranlib")
set(CMAKE_LINKER       "${JENNY_ROOT}/bin/ld.lld"      CACHE STRING "Linker")

# Use lld for linking.
add_link_options(-fuse-ld=${JENNY_ROOT}/bin/ld.lld)
