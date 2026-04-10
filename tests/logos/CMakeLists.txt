# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos compiler test suite — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

set(TEST_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)
set(STDLIB      ${CMAKE_SOURCE_DIR}/stdlib)

# ── Pass tests ────────────────────────────────────────────────────────────────
# Both .logos and .expected live in tests/logos/pass/.

file(GLOB LOCAL_PASS_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pass/*.expected)

set(LOCAL_STDLIB_USERS
    format_basic format_types variadic_format
    hello stdlib_demo option_chain)
set(LOCAL_PUBLIB_USERS pub_cross_package)
set(PUB_LIB ${CMAKE_CURRENT_SOURCE_DIR}/pub_lib)

foreach(EXP_FILE IN LISTS LOCAL_PASS_EXPECTED)
    get_filename_component(BASE ${EXP_FILE} NAME_WE)
    set(LOGOS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pass/${BASE}.logos)

    if (NOT EXISTS ${LOGOS_FILE})
        message(WARNING "logos test: no .logos for ${EXP_FILE}, skipping")
        continue()
    endif()

    if (BASE IN_LIST LOCAL_STDLIB_USERS)
        set(EXTRA -I ${STDLIB})
    elseif (BASE IN_LIST LOCAL_PUBLIB_USERS)
        set(EXTRA -I ${PUB_LIB})
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

    set(FAIL_PUBLIB_USERS pub_call_private pub_private_field_access)
    if (BASE IN_LIST FAIL_PUBLIB_USERS)
        set(FAIL_EXTRA -I ${PUB_LIB})
    else()
        set(FAIL_EXTRA "")
    endif()

    add_test(
        NAME  logos_fail_${BASE}
        COMMAND ${TEST_RUNNER} fail $<TARGET_FILE:logosc>
                ${LOGOS_FILE} ${EXP_FILE} ${FAIL_EXTRA}
    )
    set_tests_properties(logos_fail_${BASE} PROPERTIES
        TIMEOUT 15
        LABELS  "logos;fail"
    )
endforeach()
