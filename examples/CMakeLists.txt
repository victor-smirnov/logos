# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos examples — compiled to runnable executables.

if (NOT TARGET logosc)
    message(STATUS "logos examples: logosc not built — skipping")
    return()
endif()

set(STDLIB ${CMAKE_SOURCE_DIR}/stdlib)
set(STDLIB_BIN_DIR ${CMAKE_BINARY_DIR}/stdlib_bin)

# Rebuild when any Hermes stdlib file changes.
file(GLOB HERMES_STDLIB CONFIGURE_DEPENDS ${STDLIB}/hermes/*.logos)

# `metaprog` examples additionally pull in std.compiler.metaprog and link
# against libstdlib_rt.a, which carries weak stubs for the host-only
# metaprog externs (logos_emit_source, logos_get_module_ast_oview, …).
function(logos_add_example NAME)
    set(_options METAPROG)
    cmake_parse_arguments(EX "${_options}" "" "" ${ARGN})

    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.logos)
    set(OBJ ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.o)
    set(BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME})

    set(_compile_extra)
    set(_link_extra)
    set(_compile_deps logosc ${SRC} ${HERMES_STDLIB})
    if (EX_METAPROG)
        set(_compile_extra -L ${STDLIB_BIN_DIR})
        set(_link_extra ${STDLIB_BIN_DIR}/libstdlib_rt.a)
        list(APPEND _compile_deps stdlib_binary)
    endif()

    add_custom_command(
        OUTPUT ${OBJ}
        COMMAND $<TARGET_FILE:logosc> ${SRC} -I ${STDLIB} ${_compile_extra} -o ${OBJ}
        DEPENDS ${_compile_deps}
        COMMENT "logosc: ${NAME}.logos"
    )

    add_custom_command(
        OUTPUT ${BIN}
        COMMAND ${CMAKE_CXX_COMPILER} ${OBJ} ${_link_extra} -o ${BIN}
        DEPENDS ${OBJ} ${_link_extra}
        COMMENT "link: ${NAME}"
    )

    add_custom_target(example_${NAME} ALL DEPENDS ${BIN})
endfunction()

logos_add_example(hermes_round_trip)
logos_add_example(hermes_showcase)
logos_add_example(logos_showcase METAPROG)
logos_add_example(pmap_showcase METAPROG)
