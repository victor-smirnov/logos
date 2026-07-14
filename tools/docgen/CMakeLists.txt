# Build the docgen binary from tools/docgen/docgen.logos.
# docgen resolves a .docwr doc-facts container (from `logosc --emit-docs`) into
# docs.json (ADR 0014). Single-file Logos program — no sub-package archive.

if (NOT TARGET logosc)
    message(STATUS "docgen: logosc not built — skipping")
    return()
endif()

set(DOCGEN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/docgen.logos)
set(DOCGEN_OBJ ${CMAKE_CURRENT_BINARY_DIR}/docgen.o)
set(DOCGEN_BIN ${CMAKE_BINARY_DIR}/bin/docgen)

set(_link_archives
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-lcm.a
    ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
    ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a)
set(_extra_link_libs -lpthread -lm)
if(LOGOS_BUNDLE_URING_STATIC)
    list(APPEND _link_archives ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a)
else()
    list(APPEND _extra_link_libs -luring)
endif()

add_custom_command(
    OUTPUT ${DOCGEN_OBJ}
    COMMAND $<TARGET_FILE:logosc> ${DOCGEN_SRC} -o ${DOCGEN_OBJ}
    DEPENDS logosc ${DOCGEN_SRC}
            stdlib_binary stdlib_uring logos_rt logos_fibers
    COMMENT "logosc: tools/docgen/docgen.logos"
)

add_custom_command(
    OUTPUT ${DOCGEN_BIN}
    COMMAND ${CMAKE_CXX_COMPILER} -Wl,--gc-sections ${DOCGEN_OBJ} -Wl,--start-group ${_link_archives} -Wl,--end-group -Wl,--allow-multiple-definition ${_extra_link_libs} -o ${DOCGEN_BIN}
    DEPENDS ${DOCGEN_OBJ} ${_link_archives}
    COMMENT "link: docgen"
)

add_custom_target(docgen ALL DEPENDS ${DOCGEN_BIN})
