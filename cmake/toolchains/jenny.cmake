# cmake/toolchains/jenny.cmake
#
# Jenny toolchain: Victor Smirnov's Clang 21 fork with [[clang::green]] support.
# Loaded via VCPKG_CHAINLOAD_TOOLCHAIN_FILE so vcpkg integration still works.

set(JENNY_ROOT "/opt/jenny-19x" CACHE PATH "Jenny (custom Clang 19) installation root")

set(CMAKE_C_COMPILER   "${JENNY_ROOT}/bin/clang"   CACHE STRING "C compiler")
set(CMAKE_CXX_COMPILER "${JENNY_ROOT}/bin/clang++" CACHE STRING "C++ compiler")
set(CMAKE_AR           "${JENNY_ROOT}/bin/llvm-ar"     CACHE STRING "Archiver")
set(CMAKE_RANLIB       "${JENNY_ROOT}/bin/llvm-ranlib" CACHE STRING "Ranlib")
set(CMAKE_LINKER       "${JENNY_ROOT}/bin/ld.lld"      CACHE STRING "Linker")

# Use lld for linking — required for split-stack / green fiber support.
# --no-split-stack-adjust: Jenny handles green→red stack switching automatically;
# lld's split-stack prologue relaxation is unnecessary and harmful (replaces
# prologues with stc pattern → infinite __morestack loop).
add_link_options(-fuse-ld=${JENNY_ROOT}/bin/ld.lld -Wl,--no-split-stack-adjust)
