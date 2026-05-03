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

# Build libstdlib.a from the stdlib module manifest.
set(STDLIB_MODULE_MANIFEST ${CMAKE_SOURCE_DIR}/stdlib/logos.module)
set(STDLIB_BIN_DIR ${CMAKE_BINARY_DIR}/stdlib_bin)
set(STDLIB_BIN ${STDLIB_BIN_DIR}/libstdlib.a)
file(GLOB_RECURSE STDLIB_SOURCES ${CMAKE_SOURCE_DIR}/stdlib/*.logos)
file(MAKE_DIRECTORY ${STDLIB_BIN_DIR})
add_custom_command(
    OUTPUT  ${STDLIB_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${STDLIB_MODULE_MANIFEST}
            -o ${STDLIB_BIN}
    DEPENDS logosc ${STDLIB_MODULE_MANIFEST} ${STDLIB_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building binary stdlib: libstdlib.a"
)
add_custom_target(stdlib_binary ALL DEPENDS ${STDLIB_BIN})

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
    derive_debug_e2e
    derive_debug_enum
    derive_debug_generic
    item_list_splice
    multi_derive_e2e
    quote_item_smoke
    quote_item_antiquot
    quote_item_nested_antiquot
    metacall_expr_blob
    quote_expr_smoke
    quote_expr_antiquot
    quote_expr_repeat_andand
    quote_expr_repeat_comma
    quote_expr_subblob
    slice6_field_accessors
    meta_pack_basic
    meta_pack_search
    meta_kind_preds
    meta_field_iter
    meta_fold_i64
    meta_field_byname
    meta_type_methods
    meta_kind_name
    quote_ty_smoke
    meta_args_count
    meta_align_of
    meta_tuple_intrinsics
    meta_has_trait
    meta_typelist
    meta_reify_type
    meta_apply
    meta_quote_ty_antiquot
    meta_quote_ty_tup_arr
    meta_variant_intrinsics
    meta_generic_handle
    meta_type_align
    meta_has_trait_of
    meta_quote_ty_pack
    meta_template_handle
    meta_typelist_a_side
    meta_fold_to_type
    meta_template_of_smoke
    meta_template_of_typed
    meta_transform
    meta_filter
    meta_const_pack
    meta_sizeof_pack_arg
    pub_reexport
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
    hermes_check
    hermes_import_export
    hermes_hbs
    hermes_lit_parse
    assoc_const_hermes_static
    hermes_sdn_lit
    hermes_typed_arr_lit
    hermes_typed_map_lit
    hermes_capture_scalar
    hermes_capture_str
    hermes_capture_f64
    hermes_param_roundtrip
    hermes_as_array
    hermes_as_map
    hermes_typed_array_variants
    hermes_as_array_variants
    hermes_as_map_variants
    hermes_ctr_view
    hermes_typed_array_all_elems
    hermes_typed_map_all_keys
    hermes_typed_container_clone
    hermes_typed_container_equal
    hermes_view_from_static hermes_typed_push hermes_list_comp_basic hermes_list_comp_edge hermes_map_comp_basic hermes_map_comp_multi hermes_comp_coerce hermes_match_scalar hermes_match_or hermes_match_str hermes_match_map hermes_match_arr hermes_match_nested hermes_match_bind hermes_match_rest hermes_match_typed hermes_match_or_struct array_i32_generic_api array_generic_push_grow hermes_typed_array_grow
    fiber_basic fiber_yield fiber_future
    fiber_thread_basic fiber_thread_future
    fiber_chan_basic fiber_latch_basic fls_tls_basic
    auto_trait_send_scalar auto_trait_send_struct auto_trait_unsafe_impl auto_trait_generic_propagation
    blanket_impl_unbounded blanket_impl_assoc_eq instantiate_decl
    vec_full hashset_basic vecdeque_basic btreemap_basic
    atomic_basic mutex_basic
    box_basic rc_basic arc_basic bytes_basic box_dyn_trait_basic
    io_uring_basic
    fs_basic env_basic io_print_basic panic_basic pipe_basic io_read_write_trait io_writev_basic
    path_basic process_basic net_basic
    math_basic random_basic thread_basic bufio_basic bufio_generic_reader
    persistent_uid256_basic persistent_buffer_uid256 persistent_node_basic persistent_map_basic persistent_map_upsert persistent_map_cow persistent_map_split persistent_map_primitive_key persistent_set_basic persistent_store_basic persistent_store_basic
    signal_basic os_basic
    hex_basic base64_basic url_basic url_parse_basic http_types_basic http_parse_basic http_parse_corpus http_golden_suite http_parse_stream http_serialize_stream http_service_basic http_serve_bounded http_keepalive http_workers_basic http_chunked_body_helper http_chunked http_chunked_reader http_framing_readers http_roundtrip json_basic log_basic args_basic crypto_basic testing_basic csv_basic datetime_basic regex_basic)

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
        format_basic format_types variadic_format debug_primitives meta_has_trait
        hello stdlib_demo option_chain try_option mem_take_basic if_while_let closure_in_struct_field string_basic std_hash_basic std_cmp_basic annotation_has_get
        std_hashmap_basic std_hashmap_full std_hashmap_iter std_hashmap_multi_k
        iter_basic
        iter_adapters
        copy_let_independence
        for_iter_range
        list_comp_basic
        map_comp_basic
        reflect_api
        reflect_annotated_auto)
    set(LOCAL_PUBLIB_USERS pub_cross_package pkg_multifile pub_reexport)
    set(LOCAL_METAPROG_PUBLIB_USERS metacall_item_use_inherit)
    # Tests that use std.compiler.metaprog (excluded from binary stdlib;
    # metaprog_stubs in libstdlib_rt.a satisfies the link).
    set(LOCAL_METAPROG_USERS pmap_v2_assembled_lookup pmap_v2_fanout_drives_layout pmap_v2_store_basic assembler_last_wins assembler_method_set pmap_v2_assembled_client hstatic_const_generic_uid hstatic_const_generic_extract hstatic_const_generic_alias pmap_v2_skeleton pmap_v2_leaf_iter pmap_v2_point_ops_full pmap_v2_branch_leaf_asym pmap_v2_find_iter node_cfg_hash_typeless_read pmap_v2_container_directory_dispatch showcase_type_metaprog hermes_type_lit_roundtrip derive_debug_e2e derive_debug_enum derive_debug_generic item_list_splice metacall_item_list metaprog_gensym_smoke metaprog_type_to_ast multi_derive_e2e quote_item_smoke quote_item_antiquot quote_item_nested_antiquot quote_item_exprblob_splice metacall_expr_blob metacall_item_smoke metacall_item_generic_smoke metacall_item_odr_dedup metacall_item_instantiate quote_expr_smoke quote_expr_antiquot quote_expr_repeat_andand quote_expr_repeat_comma quote_expr_repeat_slice quote_expr_subblob slice6_field_accessors meta_pack_basic meta_pack_search meta_kind_preds meta_field_iter meta_fold_i64 meta_field_byname meta_type_methods meta_kind_name quote_ty_smoke meta_args_count meta_tuple_intrinsics meta_has_trait meta_typelist meta_reify_type meta_apply meta_quote_ty_antiquot meta_quote_ty_tup_arr meta_variant_intrinsics meta_generic_handle meta_type_align meta_has_trait_of meta_quote_ty_pack meta_template_handle meta_typelist_a_side meta_fold_to_type meta_template_of_smoke meta_template_of_typed template_item_handle_smoke meta_transform meta_filter quote_struct_lit_cursor derive_clone_quote derive_clone_stdlib)
    set(LOCAL_HERMES_USERS datatype_zone_basic datatype_zone_ref datatype_zone_ergonomic datatype_own datatype_relptr anyval_basic hermes_containers hermes_string hermes_typetag hermes_objectmap hermes_document hermes_stringify hermes_ex_arena hermes_ex_containers hermes_parse hermes_round_trip hermes_stringbuf_grow hermes_anyval_methods hermes_ctr_traversal hermes_float_parse hermes_escape_strings hermes_builder hermes_registry_lookup hermes_traits_full hermes_traits_bugfix hermes_scalar hermes_decimal_methods hermes_decimal_limits_errors hermes_decimal_f64 hermes_compactify hermes_clone hermes_buffer_i64 hermes_buffer_string hermes_string_view_interop datatag_typecode tag_system_basic tag_dispatch_table data_plain_check tag_dispatch_call tag_dispatch_tier2 tag_dispatch_sibling_concrete hermes_zone_seal hermes_memholder_rc podref_buffer mem_holder_custom_destroyer array_i32_polymorphic hermes_parse_typed_containers hermes_typed_array_materialize hermes_typed_array_element_type_errors hermes_typed_datatype_values hermes_typed_datatype_values_errors hermes_typed_datatype_trait_coverage hermes_typed_datatype_params_overflow hermes_typed_datatype_empty_params_canonical hermes_typed_datatype_empty_params_small_zone hermes_typed_datatype_params_trailing_comma_errors hermes_typed_decimal_validation_pass hermes_typed_decimal_validation_errors hermes_typed_map_default_materialize hermes_typed_map_i32_materialize hermes_typed_map_key_type_errors hermes_typed_materialization_errors array_relptr_strings hermes_check hermes_import_export hermes_hbs dec_arithmetic dec_boundary dec_deep dec_edge_cases hermes_sdn_lit hermes_typed_arr_lit hermes_typed_map_lit hermes_typed_array_all_elems hermes_typed_map_all_keys hermes_typed_container_clone hermes_typed_container_equal hermes_capture_scalar hermes_capture_str hermes_capture_f64 hermes_param_roundtrip hermes_as_array hermes_as_map hermes_typed_array_variants hermes_as_array_variants hermes_as_map_variants hermes_ctr_view hermes_view_from_static hermes_typed_push hermes_list_comp_basic hermes_list_comp_edge hermes_map_comp_basic hermes_map_comp_multi hermes_comp_coerce hermes_match_scalar hermes_match_or hermes_match_str hermes_match_map hermes_match_arr hermes_match_nested hermes_match_bind hermes_match_rest hermes_match_typed hermes_match_or_struct array_i32_generic_api array_generic_push_grow hermes_typed_array_grow reflect_basic meta_struct meta_trait genos_reflect metacall_hermes_static metacall_hermes fiber_basic fiber_yield fiber_future fiber_thread_basic fiber_thread_future fiber_chan_basic fiber_latch_basic fls_tls_basic fiber_reactor_basic fiber_sleep_ms auto_trait_send_scalar auto_trait_send_struct auto_trait_unsafe_impl auto_trait_generic_propagation assoc_const_hermes_static
    vec_full hashset_basic vecdeque_basic btreemap_basic
    atomic_basic mutex_basic
    box_basic rc_basic arc_basic bytes_basic box_dyn_trait_basic
    chain_field_write chain_field_write_deep
    meta_traits_basic
    utf8_basic
    time_basic
    io_uring_basic
    fs_basic env_basic io_print_basic panic_basic pipe_basic io_read_write_trait io_writev_basic
    path_basic process_basic net_basic
    math_basic random_basic thread_basic bufio_basic bufio_generic_reader
    persistent_uid256_basic persistent_buffer_uid256 persistent_node_basic persistent_map_basic persistent_map_upsert persistent_map_cow persistent_map_split persistent_map_primitive_key persistent_set_basic persistent_store_basic persistent_store_basic
    signal_basic os_basic
    blanket_impl_sibling_bound_resolution blanket_chain_two_hops blanket_chain_assoc_eq blanket_chain_subst generic_chain_write_through_ptr
    generic_ref_registry_shape generic_ref_registry_dispatch
    hex_basic base64_basic url_basic url_parse_basic http_types_basic http_parse_basic http_parse_corpus http_golden_suite http_parse_stream http_serialize_stream http_service_basic http_serve_bounded http_keepalive http_workers_basic http_chunked_body_helper http_chunked http_chunked_reader http_framing_readers http_roundtrip json_basic log_basic args_basic crypto_basic testing_basic csv_basic datetime_basic regex_basic)

    if (BASE IN_LIST LOCAL_STDLIB_USERS)
        set(_extra -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST LOCAL_METAPROG_USERS)
        set(_extra -I ${STDLIB} -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST LOCAL_METAPROG_PUBLIB_USERS)
        set(_extra -I ${STDLIB} -I ${PUB_LIB} -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST LOCAL_PUBLIB_USERS)
        set(_extra -I ${PUB_LIB} -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST LOCAL_HERMES_USERS)
        set(_extra -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    else()
        set(_extra "")
    endif()

    set(${OUT_VAR} "${_extra}" PARENT_SCOPE)
endfunction()

function(logos_fail_extra_args BASE OUT_VAR)
    set(FAIL_PUBLIB_USERS pub_call_private pub_private_field_access)
    set(FAIL_HERMES_USERS hermes_capture_bad_type hermes_capture_typed_array hermes_as_array_bad_type hermes_as_array_not_slice hermes_as_array_elem_mismatch hermes_as_map_wrong_src hermes_comp_coerce_bad hermes_map_comp_guard_bad auto_trait_send_raw_ptr auto_trait_negative_impl auto_trait_bound_check_target blanket_impl_overlap blanket_impl_assoc_eq_mismatch instantiate_decl_unknown)
    set(FAIL_METAPROG_USERS metacall_item_odr_conflict)
    if (BASE IN_LIST FAIL_PUBLIB_USERS)
        set(_extra -I ${PUB_LIB} -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST FAIL_HERMES_USERS)
        set(_extra -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
    elseif (BASE IN_LIST FAIL_METAPROG_USERS)
        set(_extra -I ${STDLIB} -I ${STDLIB_BIN_DIR} -L ${STDLIB_BIN_DIR})
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

# ── IR snapshot tests ─────────────────────────────────────────────────────────
# Each .check file in ir/ is paired with a same-named .logos. The snapshot
# runner pipes `logosc --emit-llvm` output through FileCheck against the
# .check spec. Goal: catch monomorphization / codegen regressions on hot
# paths without needing full golden IR diffs.

find_program(FILECHECK_BIN
    NAMES FileCheck FileCheck-19 FileCheck-18
    PATHS /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin
    DOC "LLVM FileCheck for IR snapshot tests")

if (FILECHECK_BIN)
    set(IR_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/ir/run_ir_snapshot.sh)
    file(GLOB IR_CHECK_FILES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/ir/*.check)
    list(SORT IR_CHECK_FILES)

    foreach(CHK_FILE IN LISTS IR_CHECK_FILES)
        get_filename_component(BASE ${CHK_FILE} NAME_WE)
        set(LOGOS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/ir/${BASE}.logos)
        if (NOT EXISTS ${LOGOS_FILE})
            message(WARNING "ir snapshot: no .logos for ${CHK_FILE}, skipping")
            continue()
        endif()
        set(_test_name "logos_07_ir_snapshot_${BASE}")
        add_test(
            NAME ${_test_name}
            COMMAND ${IR_RUNNER}
                    $<TARGET_FILE:logosc>
                    ${FILECHECK_BIN}
                    ${LOGOS_FILE}
                    ${CHK_FILE}
                    -I ${STDLIB}
                    -I ${STDLIB_BIN_DIR}
        )
        set_tests_properties(${_test_name} PROPERTIES
            TIMEOUT 30
            LABELS  "logos;ir_snapshot;suite_ir"
        )
    endforeach()
else()
    message(STATUS "logos tests: FileCheck not found — skipping IR snapshot suite")
endif()
