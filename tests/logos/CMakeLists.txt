# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos compiler test suite — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

set(TEST_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)
set(EXAMPLES    ${CMAKE_SOURCE_DIR}/examples)
set(STDLIB      ${CMAKE_SOURCE_DIR}/stdlib)

# ── Pass tests ────────────────────────────────────────────────────────────────
# Each .expected file in pass/ matches a .logos file in examples/.

foreach(EXPECTED IN ITEMS
    pass/quicksort.expected
    pass/while_test.expected
    pass/point.expected
    pass/hello.expected
)
    get_filename_component(BASE ${EXPECTED} NAME_WE)  # e.g. "quicksort"
    set(LOGOS_FILE ${EXAMPLES}/${BASE}.logos)
    set(EXP_FILE   ${CMAKE_CURRENT_SOURCE_DIR}/${EXPECTED})

    if (BASE STREQUAL "hello")
        set(EXTRA -I ${STDLIB})
    else()
        set(EXTRA "")
    endif()

    add_test(
        NAME  logos_pass_${BASE}
        COMMAND ${TEST_RUNNER} pass $<TARGET_FILE:logosc>
                ${LOGOS_FILE} ${EXP_FILE} ${EXTRA}
    )
    set_tests_properties(logos_pass_${BASE} PROPERTIES
        TIMEOUT 30
        LABELS  "logos;pass"
    )
endforeach()

# ── Fail tests ────────────────────────────────────────────────────────────────
# Each .expected file in fail/ matches a .logos file in the same directory.

file(GLOB FAIL_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/fail/*.expected)

foreach(EXP_FILE IN LISTS FAIL_EXPECTED)
    get_filename_component(BASE ${EXP_FILE} NAME_WE)
    set(LOGOS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/fail/${BASE}.logos)

    if (NOT EXISTS ${LOGOS_FILE})
        message(WARNING "logos test: no .logos for ${EXP_FILE}, skipping")
        continue()
    endif()

    add_test(
        NAME  logos_fail_${BASE}
        COMMAND ${TEST_RUNNER} fail $<TARGET_FILE:logosc>
                ${LOGOS_FILE} ${EXP_FILE}
    )
    set_tests_properties(logos_fail_${BASE} PROPERTIES
        TIMEOUT 15
        LABELS  "logos;fail"
    )
endforeach()
