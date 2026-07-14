# Build the lforge binary from tools/lforge/main.logos.

if (NOT TARGET logosc)
    message(STATUS "lforge: logosc not built — skipping")
    return()
endif()

set(LFORGE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.logos)
set(LFORGE_OBJ ${CMAKE_CURRENT_BINARY_DIR}/lforge.o)
set(LFORGE_BIN ${CMAKE_BINARY_DIR}/bin/lforge)

# Sub-packages (lforge.<name>) live in tools/lforge/pkg/ and ship as
# a binary archive consumed via `-l`. main.logos is the entry-point.
set(LFORGE_PKG_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/pkg/lforge.module)
set(LFORGE_PKG_BIN      ${CMAKE_CURRENT_BINARY_DIR}/liblforge_pkg.a)
file(GLOB LFORGE_PKG_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/*.logos)

add_custom_command(
    OUTPUT ${LFORGE_PKG_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${LFORGE_PKG_MANIFEST}
            -o ${LFORGE_PKG_BIN}
    DEPENDS logosc ${LFORGE_PKG_MANIFEST} ${LFORGE_PKG_SRCS}
            stdlib_binary stdlib_uring logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building lforge sub-package archive: liblforge_pkg.a"
)
add_custom_target(lforge_pkg_binary DEPENDS ${LFORGE_PKG_BIN})

set(_link_archives
    ${LFORGE_PKG_BIN}
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lcm.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a)
# liburing: bundled static archive where the distro ships one, else the shared
# -luring (Fedora). See stdlib/rt/CMakeLists.txt.
set(_extra_link_libs -lpthread -lm)
if(LOGOS_BUNDLE_URING_STATIC)
    list(APPEND _link_archives ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a)
else()
    list(APPEND _extra_link_libs -luring)
endif()

add_custom_command(
    OUTPUT ${LFORGE_OBJ}
    COMMAND $<TARGET_FILE:logosc>
        -l ${LFORGE_PKG_BIN}
        ${LFORGE_SRC} -o ${LFORGE_OBJ}
    DEPENDS logosc ${LFORGE_SRC} ${LFORGE_PKG_BIN}
            stdlib_binary stdlib_uring logos_rt logos_fibers
    COMMENT "logosc: tools/lforge/main.logos"
)

add_custom_command(
    OUTPUT ${LFORGE_BIN}
    COMMAND ${CMAKE_CXX_COMPILER} -Wl,--gc-sections ${LFORGE_OBJ} -Wl,--start-group ${_link_archives} -Wl,--end-group -Wl,--allow-multiple-definition ${_extra_link_libs} -o ${LFORGE_BIN}
    DEPENDS ${LFORGE_OBJ} ${_link_archives}
    COMMENT "link: lforge"
)

add_custom_target(lforge ALL DEPENDS ${LFORGE_BIN})
