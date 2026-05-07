# SPDX-License-Identifier: Apache-2.0
# Build the lforge binary from tools/lforge/main.logos.

if (NOT TARGET logosc)
    message(STATUS "lforge: logosc not built — skipping")
    return()
endif()

set(LFORGE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.logos)
set(LFORGE_OBJ ${CMAKE_CURRENT_BINARY_DIR}/lforge.o)
set(LFORGE_BIN ${CMAKE_BINARY_DIR}/bin/lforge)

set(_link_archives
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a)

add_custom_command(
    OUTPUT ${LFORGE_OBJ}
    COMMAND $<TARGET_FILE:logosc> ${LFORGE_SRC} -o ${LFORGE_OBJ}
    DEPENDS logosc ${LFORGE_SRC} stdlib_binary stdlib_uring logos_rt logos_fibers
    COMMENT "logosc: tools/lforge/main.logos"
)

add_custom_command(
    OUTPUT ${LFORGE_BIN}
    COMMAND ${CMAKE_CXX_COMPILER} ${LFORGE_OBJ} ${_link_archives} -lpthread -lm -o ${LFORGE_BIN}
    DEPENDS ${LFORGE_OBJ} ${_link_archives}
    COMMENT "link: lforge"
)

add_custom_target(lforge ALL DEPENDS ${LFORGE_BIN})
