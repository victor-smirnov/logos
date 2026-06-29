# Build the peg_gen_logos binary from tools/peg_gen_logos/main.logos.
#
# peg_gen_logos is the Logos-language sibling of peg_gen_cpp: it reads a .peg
# grammar and emits a Logos-source parser. Built like lforge (see
# tools/lforge/CMakeLists.txt): sub-packages -> binary archive via
# `logosc --emit-module`, then main.logos compiled + linked via cc.

if (NOT TARGET logosc)
    message(STATUS "peg_gen_logos: logosc not built — skipping")
    return()
endif()

set(PGL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.logos)
set(PGL_OBJ ${CMAKE_CURRENT_BINARY_DIR}/peg_gen_logos.o)
set(PGL_BIN ${CMAKE_BINARY_DIR}/bin/peg_gen_logos)

set(PGL_PKG_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/pkg/peg_gen_logos.module)
set(PGL_PKG_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libpeg_gen_logos_pkg.a)
file(GLOB PGL_PKG_SRCS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pkg/*.logos)

add_custom_command(
    OUTPUT ${PGL_PKG_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${PGL_PKG_MANIFEST}
            -o ${PGL_PKG_BIN}
    DEPENDS logosc ${PGL_PKG_MANIFEST} ${PGL_PKG_SRCS}
            stdlib_binary stdlib_uring logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building peg_gen_logos sub-package archive: libpeg_gen_logos_pkg.a"
)
add_custom_target(peg_gen_logos_pkg_binary DEPENDS ${PGL_PKG_BIN})

set(_pgl_link_archives
    ${PGL_PKG_BIN}
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a)
set(_pgl_extra_link_libs -lpthread -lm)
if(LOGOS_BUNDLE_URING_STATIC)
    list(APPEND _pgl_link_archives ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a)
else()
    list(APPEND _pgl_extra_link_libs -luring)
endif()

add_custom_command(
    OUTPUT ${PGL_OBJ}
    COMMAND $<TARGET_FILE:logosc>
        -l ${PGL_PKG_BIN}
        ${PGL_SRC} -o ${PGL_OBJ}
    DEPENDS logosc ${PGL_SRC} ${PGL_PKG_BIN}
            stdlib_binary stdlib_uring logos_rt logos_fibers
    COMMENT "logosc: tools/peg_gen_logos/main.logos"
)

add_custom_command(
    OUTPUT ${PGL_BIN}
    COMMAND ${CMAKE_CXX_COMPILER} -Wl,--gc-sections ${PGL_OBJ} -Wl,--start-group ${_pgl_link_archives} -Wl,--end-group -Wl,--allow-multiple-definition ${_pgl_extra_link_libs} -o ${PGL_BIN}
    DEPENDS ${PGL_OBJ} ${_pgl_link_archives}
    COMMENT "link: peg_gen_logos"
)

add_custom_target(peg_gen_logos ALL DEPENDS ${PGL_BIN})
