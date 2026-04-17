# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Victor Smirnov
# Logos compiler test suite — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

set(TEST_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)
set(STDLIB      ${CMAKE_SOURCE_DIR}/stdlib)
set(PUB_LIB     ${CMAKE_CURRENT_SOURCE_DIR}/pub_lib)

set(LOGOS_SUITE_ORDER
    smoke
    semantic_core
    ownership
    advanced_features
    integration
    diagnostics)

set(LOGOS_SMOKE_TESTS
    # pass: core language features — one representative per category
    arith_precedence
    fn_default_ret
    if_expr
    while_test
    for_sum
    loop_continue          # RC4 fix: continue statement
    g6_struct_impl         # RC3 fix: impl method call (not just field access)
    enum_match
    generic_identity
    g1_trait_impl
    closure_with_capture   # RC2 fix: closure that captures a variable
    # fail: one representative per error class
    stray_token
    type_mismatch
    wrong_arg_count
    undef_var
    assign_immutable       # RC5 fix: assignment to non-mut variable
    # fail: parse errors (one per scenario, not per token type)
    invalid_char_after_package)

set(LOGOS_INTEGRATION_TESTS
    bst
    fibonacci
    quicksort
    linked_list
    stdlib_demo
    vec_usage
    format_basic
    format_types
    datatype_zone_basic
    datatype_zone_ref
    datatype_zone_ergonomic
    datatype_impl
    datatype_own
    datatype_relptr
    hermes_string
    hermes_typetag
    hermes_objectmap
    hermes_document
    hermes_stringify
    hermes_ex_arena
    hermes_ex_containers
    hermes_parse
    hermes_round_trip
    hermes_stringbuf_grow
    hermes_anyval_methods
    hermes_ctr_traversal
    hermes_builder
    hermes_registry_lookup
    hermes_traits_full
    hermes_traits_bugfix
    hermes_scalar
    hermes_buffer_i64
    hermes_buffer_string
    hermes_string_view_interop
    hermes_zone_seal
    hermes_memholder_rc
    hermes_check)

function(logos_determine_suite MODE BASE OUT_VAR)
    if (BASE IN_LIST LOGOS_SMOKE_TESTS)
        set(_suite smoke)
    elseif (BASE IN_LIST LOGOS_INTEGRATION_TESTS)
        set(_suite integration)
    elseif (BASE MATCHES
        "^(borrow_|clone_|copy_|drop_|lifetime_|unsafe_|cond_ref_|deref_|move_|mut_|shared_|dangling_ref|assign_ref_dangling|double_mut_borrow|use_after_move|use_while_mut_borrowed|const_ptr_write)")
        set(_suite ownership)
    elseif (BASE MATCHES
        "^(assoc_|generic_|trait_|dyn_|variadic_|closure_|impl_|default_|g1_trait_impl|g1_standalone_impl|g1_extern_fn|g2_impl_trait_return|impl_trait_|from_|into_|option_chain|spec_dispatch|extern_)")
        set(_suite advanced_features)
    elseif (MODE STREQUAL fail)
        set(_suite diagnostics)
    else()
        set(_suite semantic_core)
    endif()

    set(${OUT_VAR} ${_suite} PARENT_SCOPE)
endfunction()

function(logos_pass_extra_args BASE OUT_VAR)
    set(LOCAL_STDLIB_USERS
        format_basic format_types variadic_format
        hello stdlib_demo option_chain string_basic)
    set(LOCAL_PUBLIB_USERS pub_cross_package)
    set(LOCAL_HERMES_USERS datatype_zone_basic datatype_zone_ref datatype_zone_ergonomic datatype_own datatype_relptr anyval_basic hermes_containers hermes_string hermes_typetag hermes_objectmap hermes_document hermes_stringify hermes_ex_arena hermes_ex_containers hermes_parse hermes_round_trip hermes_stringbuf_grow hermes_anyval_methods hermes_ctr_traversal hermes_float_parse hermes_escape_strings hermes_builder hermes_registry_lookup hermes_traits_full hermes_traits_bugfix hermes_scalar hermes_decimal_methods hermes_decimal_limits_errors hermes_decimal_f64 hermes_compactify hermes_clone hermes_buffer_i64 hermes_buffer_string hermes_string_view_interop datatag_typecode tag_system_basic tag_dispatch_table data_plain_check tag_dispatch_call tag_dispatch_tier2 hermes_zone_seal hermes_memholder_rc podref_buffer mem_holder_custom_destroyer array_i32_polymorphic hermes_parse_typed_containers hermes_typed_array_materialize hermes_typed_array_element_type_errors hermes_typed_datatype_values hermes_typed_datatype_values_errors hermes_typed_datatype_trait_coverage hermes_typed_datatype_params_overflow hermes_typed_datatype_empty_params_canonical hermes_typed_datatype_empty_params_small_zone hermes_typed_datatype_params_trailing_comma_errors hermes_typed_decimal_validation_pass hermes_typed_decimal_validation_errors hermes_typed_map_default_materialize hermes_typed_map_i32_materialize hermes_typed_map_key_type_errors hermes_typed_materialization_errors array_relptr_strings hermes_check)

    if (BASE IN_LIST LOCAL_STDLIB_USERS)
        set(_extra -I ${STDLIB})
    elseif (BASE IN_LIST LOCAL_PUBLIB_USERS)
        set(_extra -I ${PUB_LIB})
    elseif (BASE IN_LIST LOCAL_HERMES_USERS)
        set(_extra -I ${STDLIB})
    else()
        set(_extra "")
    endif()

    set(${OUT_VAR} "${_extra}" PARENT_SCOPE)
endfunction()

function(logos_fail_extra_args BASE OUT_VAR)
    set(FAIL_PUBLIB_USERS pub_call_private pub_private_field_access)
    if (BASE IN_LIST FAIL_PUBLIB_USERS)
        set(_extra -I ${PUB_LIB})
    else()
        set(_extra "")
    endif()

    set(${OUT_VAR} "${_extra}" PARENT_SCOPE)
endfunction()

function(logos_add_grouped_test MODE BASE EXP_FILE LOGOS_FILE SUITE)
    if (SUITE STREQUAL smoke)
        set(_suite_prefix "01")
    elseif (SUITE STREQUAL semantic_core)
        set(_suite_prefix "02")
    elseif (SUITE STREQUAL ownership)
        set(_suite_prefix "03")
    elseif (SUITE STREQUAL advanced_features)
        set(_suite_prefix "04")
    elseif (SUITE STREQUAL integration)
        set(_suite_prefix "05")
    else()
        set(_suite_prefix "06")
    endif()

    set(_test_name "logos_${_suite_prefix}_${SUITE}_${MODE}_${BASE}")

    if (MODE STREQUAL pass)
        logos_pass_extra_args(${BASE} EXTRA)
        set(_timeout 30)
    else()
        logos_fail_extra_args(${BASE} EXTRA)
        set(_timeout 15)
    endif()

    add_test(
        NAME  ${_test_name}
        COMMAND ${TEST_RUNNER} ${MODE} $<TARGET_FILE:logosc>
                ${LOGOS_FILE} ${EXP_FILE} ${EXTRA}
    )
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT ${_timeout}
        LABELS  "logos;${MODE};suite_${SUITE}"
    )
endfunction()

# ── Pass tests ────────────────────────────────────────────────────────────────
# Both .logos and .expected live in tests/logos/pass/.

file(GLOB LOCAL_PASS_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pass/*.expected)
list(SORT LOCAL_PASS_EXPECTED)

# ── Fail tests ────────────────────────────────────────────────────────────────
# Each .expected file in fail/ matches a .logos file in the same directory.

file(GLOB FAIL_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/fail/*.expected)
list(SORT FAIL_EXPECTED)

# Register tests in suite order so plain `ctest` runs them in grouped batches.
foreach(SUITE IN LISTS LOGOS_SUITE_ORDER)
    foreach(EXP_FILE IN LISTS LOCAL_PASS_EXPECTED)
        get_filename_component(BASE ${EXP_FILE} NAME_WE)
        set(LOGOS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/pass/${BASE}.logos)

        if (NOT EXISTS ${LOGOS_FILE})
            message(WARNING "logos test: no .logos for ${EXP_FILE}, skipping")
            continue()
        endif()

        logos_determine_suite(pass ${BASE} TEST_SUITE)
        if (TEST_SUITE STREQUAL SUITE)
            logos_add_grouped_test(pass ${BASE} ${EXP_FILE} ${LOGOS_FILE} ${SUITE})
        endif()
    endforeach()

    foreach(EXP_FILE IN LISTS FAIL_EXPECTED)
        get_filename_component(BASE ${EXP_FILE} NAME_WE)
        set(LOGOS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/fail/${BASE}.logos)

        if (NOT EXISTS ${LOGOS_FILE})
            message(WARNING "logos test: no .logos for ${EXP_FILE}, skipping")
            continue()
        endif()

        logos_determine_suite(fail ${BASE} TEST_SUITE)
        if (TEST_SUITE STREQUAL SUITE)
            logos_add_grouped_test(fail ${BASE} ${EXP_FILE} ${LOGOS_FILE} ${SUITE})
        endif()
    endforeach()
endforeach()
