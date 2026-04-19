# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos examples — compiled to runnable executables.

if (NOT TARGET logosc)
    message(STATUS "logos examples: logosc not built — skipping")
    return()
endif()

set(STDLIB ${CMAKE_SOURCE_DIR}/stdlib)

# Rebuild when any Hermes stdlib file changes.
file(GLOB HERMES_STDLIB CONFIGURE_DEPENDS ${STDLIB}/hermes/*.logos)

function(logos_add_example NAME)
    set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/${NAME}.logos)
    set(OBJ ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.o)
    set(BIN ${CMAKE_CURRENT_BINARY_DIR}/${NAME})

    add_custom_command(
        OUTPUT ${OBJ}
        COMMAND $<TARGET_FILE:logosc> ${SRC} -I ${STDLIB} -o ${OBJ}
        DEPENDS logosc ${SRC} ${HERMES_STDLIB}
        COMMENT "logosc: ${NAME}.logos"
    )

    add_custom_command(
        OUTPUT ${BIN}
        COMMAND ${CMAKE_CXX_COMPILER} ${OBJ} -o ${BIN}
        DEPENDS ${OBJ}
        COMMENT "link: ${NAME}"
    )

    add_custom_target(example_${NAME} ALL DEPENDS ${BIN})
endfunction()

logos_add_example(hermes_round_trip)
logos_add_example(hermes_showcase)
