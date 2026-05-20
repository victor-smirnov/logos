# Copyright 2026 Victor Smirnov
# Logos examples — compiled to runnable executables.

if (NOT TARGET logosc)
    message(STATUS "logos examples: logosc not built — skipping")
    return()
endif()

set(STDLIB ${CMAKE_SOURCE_DIR}/stdlib)
set(STDLIB_BIN_DIR ${LOGOS_BUILD_LIB_DIR})

# Rebuild when any Hermes stdlib file changes.
file(GLOB HERMES_STDLIB CONFIGURE_DEPENDS ${STDLIB}/hermes/*.logos)

# `metaprog` examples additionally link against liblstdlib_rt.a (asm /
# weak metaprog stubs) since some of those symbols are referenced from
# the user-level code path (fiber switch, etc.).  The system module
# library is auto-discovered by logosc and linked transparently below.
function(logos_add_example NAME)
    set(_options METAPROG)
    cmake_parse_arguments(EX "${_options}" "" "" ${ARGN})

    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.logos)
    set(OBJ ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.o)
    set(BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME})

    # logosc auto-discovers the system stdlib (liblstdlib.a) via its
    # baked-in argv[0]-relative path. METAPROG examples additionally need
    # source access to std.compiler.metaprog (excluded from liblstdlib.a
    # because its bodies reference host-only externs); they get -I source.
    # Native runtime archives (fiber switch, liburing wrappers) sit
    # alongside liblstdlib.a and feed the link line.
    set(_compile_deps logosc ${SRC} ${HERMES_STDLIB} stdlib_binary stdlib_uring logos_rt logos_fibers)
    set(_compile_extra)
    set(_link_archives
        ${LOGOS_BUILD_LIB_DIR}/liblogos-lang.a
        ${LOGOS_BUILD_LIB_DIR}/liblogos-mem.a
        ${LOGOS_BUILD_LIB_DIR}/liblogos-std.a
        ${LOGOS_BUILD_LIB_DIR}/liblstdlib_fibers.a
        ${LOGOS_BUILD_LIB_DIR}/liblstdlib_rt.a
        ${LOGOS_BUILD_LIB_DIR}/liblstdlib_uring.a)

    add_custom_command(
        OUTPUT ${OBJ}
        COMMAND $<TARGET_FILE:logosc> ${SRC} ${_compile_extra} -o ${OBJ}
        DEPENDS ${_compile_deps}
        COMMENT "logosc: ${NAME}.logos"
    )

    add_custom_command(
        OUTPUT ${BIN}
        COMMAND ${CMAKE_CXX_COMPILER} -Wl,--gc-sections ${OBJ} -Wl,--start-group ${_link_archives} -Wl,--end-group -Wl,--allow-multiple-definition -lpthread -lm -lstdc++ -o ${BIN}
        DEPENDS ${OBJ} ${_link_archives}
        COMMENT "link: ${NAME}"
    )

    add_custom_target(example_${NAME} ALL DEPENDS ${BIN})
endfunction()

logos_add_example(hermes_round_trip)
logos_add_example(hermes_showcase)
logos_add_example(logos_showcase METAPROG)
logos_add_example(persistent_showcase)
