# Copyright 2026 Victor Smirnov
# Logos compiler test suite — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

set(TEST_RUNNER ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh)
set(STDLIB      ${CMAKE_SOURCE_DIR}/stdlib)
set(PUB_LIB     ${CMAKE_CURRENT_SOURCE_DIR}/pub_lib)

# Three-layer split Step 8 (2026-05-17): build order FLIPPED.
# Single modular stdlib: three layers lang → mem → std (each built with -L
# referencing the lower archives; the std layer's .o is self-contained — it
# re-embeds lang+mem — so the metacall JIT can load liblogos-std.a alone, the
# way it used the former monolith). The monolith (liblstdlib.a) was folded
# into the std layer 2026-05-20: stdlib/std now holds ALL logos.std.* packages
# (io/http/persistent/metaprog/fmt/sync/process + thread/time/crypto/...).
set(STDLIB_BIN_DIR ${LOGOS_BUILD_LIB_DIR})
set(LOGOS_LANG_MANIFEST ${CMAKE_SOURCE_DIR}/stdlib/lang/logos.module)
set(LOGOS_MEM_MANIFEST  ${CMAKE_SOURCE_DIR}/stdlib/mem/logos.module)
set(LOGOS_LCM_MANIFEST  ${CMAKE_SOURCE_DIR}/stdlib/lcm/logos.module)
set(LOGOS_STD_MANIFEST  ${CMAKE_SOURCE_DIR}/stdlib/std/logos.module)
set(LOGOS_LANG_BIN ${STDLIB_BIN_DIR}/liblogos-lang.a)
set(LOGOS_MEM_BIN  ${STDLIB_BIN_DIR}/liblogos-mem.a)
set(LOGOS_LCM_BIN  ${STDLIB_BIN_DIR}/liblogos-lcm.a)
set(LOGOS_STD_BIN  ${STDLIB_BIN_DIR}/liblogos-std.a)
file(GLOB_RECURSE LOGOS_LANG_SOURCES ${CMAKE_SOURCE_DIR}/stdlib/lang/*.logos)
file(GLOB_RECURSE LOGOS_MEM_SOURCES  ${CMAKE_SOURCE_DIR}/stdlib/mem/*.logos)
file(GLOB_RECURSE LOGOS_LCM_SOURCES  ${CMAKE_SOURCE_DIR}/stdlib/lcm/*.logos)
file(GLOB_RECURSE LOGOS_STD_SOURCES  ${CMAKE_SOURCE_DIR}/stdlib/std/*.logos)
add_custom_command(
    OUTPUT  ${LOGOS_LANG_BIN}
    COMMAND $<TARGET_FILE:logosc>
            ${LOGOS_LIB_OPT}
            --emit-module ${LOGOS_LANG_MANIFEST}
            -o ${LOGOS_LANG_BIN}
    DEPENDS $<TARGET_FILE:logosc> ${LOGOS_LANG_MANIFEST} ${LOGOS_LANG_SOURCES} logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building stdlib layer: liblogos-lang.a"
)
add_custom_command(
    OUTPUT  ${LOGOS_MEM_BIN}
    COMMAND $<TARGET_FILE:logosc>
            ${LOGOS_LIB_OPT}
            --emit-module ${LOGOS_MEM_MANIFEST}
            -L ${STDLIB_BIN_DIR}
            -o ${LOGOS_MEM_BIN}
    DEPENDS $<TARGET_FILE:logosc> ${LOGOS_MEM_MANIFEST} ${LOGOS_MEM_SOURCES} ${LOGOS_LANG_BIN} logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building stdlib layer: liblogos-mem.a (depends logos-lang via manifest)"
)
add_custom_command(
    OUTPUT  ${LOGOS_LCM_BIN}
    COMMAND $<TARGET_FILE:logosc>
            ${LOGOS_LIB_OPT}
            --emit-module ${LOGOS_LCM_MANIFEST}
            -L ${STDLIB_BIN_DIR}
            -o ${LOGOS_LCM_BIN}
    DEPENDS $<TARGET_FILE:logosc> ${LOGOS_LCM_MANIFEST} ${LOGOS_LCM_SOURCES} ${LOGOS_MEM_BIN} ${LOGOS_LANG_BIN} logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building stdlib layer: liblogos-lcm.a (depends logos-mem + logos-lang via manifest)"
)
add_custom_command(
    OUTPUT  ${LOGOS_STD_BIN}
    COMMAND $<TARGET_FILE:logosc>
            ${LOGOS_LIB_OPT}
            --emit-module ${LOGOS_STD_MANIFEST}
            -L ${STDLIB_BIN_DIR}
            -o ${LOGOS_STD_BIN}
    DEPENDS $<TARGET_FILE:logosc> ${LOGOS_STD_MANIFEST} ${LOGOS_STD_SOURCES} ${LOGOS_LCM_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LANG_BIN} logos_rt logos_fibers
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building stdlib layer: liblogos-std.a (depends logos-lcm + logos-mem + logos-lang via manifest)"
)
add_custom_target(stdlib_layers ALL DEPENDS
    ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN})

# The monolith `stdlib_binary` target is gone (folded into the std layer);
# keep the name as an alias to the layer set so existing
# `add_dependencies(... stdlib_binary)` / DEPENDS sites still resolve.
add_custom_target(stdlib_binary ALL DEPENDS
    ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN})

# Build libpub_lib.a from the pub_lib test fixture manifest. Used as a
# realistic `-l <user-archive>` smoke test in the publib suite, replacing
# the earlier `-I ${PUB_LIB}` source-include path.
set(PUB_LIB_MANIFEST ${PUB_LIB}/mylib.module)
set(PUB_LIB_BIN ${CMAKE_CURRENT_BINARY_DIR}/libpub_lib.a)
file(GLOB PUB_LIB_SOURCES ${PUB_LIB}/*.logos)
add_custom_command(
    OUTPUT  ${PUB_LIB_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${PUB_LIB_MANIFEST} -L ${STDLIB_BIN_DIR}
            -o ${PUB_LIB_BIN}
    # -L stdlib + DEPENDS on the stdlib archives: a pub_lib member (wref_field_pkg,
    # the cross-CU WRef mono-bug regression) `use`s logos.std.wql.ir + logos.lang.writ.*,
    # so the fixture must build AFTER the stdlib and see it (matches coex2a/2b). Without
    # this a clean build schedules libpub_lib before liblogos-* -> module-loader misses.
    DEPENDS logosc ${PUB_LIB_MANIFEST} ${PUB_LIB_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libpub_lib.a"
)
add_custom_target(pub_lib_binary ALL DEPENDS ${PUB_LIB_BIN})

# ADR 0016 cross-module mapping fixture: a `pub mapping` published from a
# binary module; consumers fuse its rules from the archived AST (the
# MAPPING_DEF_DONE nodes keep their identity through the emit's thunk flip).
set(WQL_MAP_LIB ${CMAKE_CURRENT_SOURCE_DIR}/wql_map_lib)
set(WQL_MAP_LIB_BIN ${CMAKE_CURRENT_BINARY_DIR}/libwql_map_lib.a)
file(GLOB WQL_MAP_LIB_SOURCES ${WQL_MAP_LIB}/*.logos)
add_custom_command(
    OUTPUT  ${WQL_MAP_LIB_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${WQL_MAP_LIB}/maplib.module -L ${STDLIB_BIN_DIR}
            -o ${WQL_MAP_LIB_BIN}
    DEPENDS logosc ${WQL_MAP_LIB}/maplib.module ${WQL_MAP_LIB_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libwql_map_lib.a"
)
add_custom_target(wql_map_lib_binary ALL DEPENDS ${WQL_MAP_LIB_BIN})

# Build libcoex.a from the cross-pkg-coexistence fixture (alpha + beta).
# Used as `-l <archive>` for the cross_pkg_coexistence /
# cross_pkg_ambiguous_call tests, replacing the earlier `-I .../coex`
# source-include path.
set(COEX_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/coex)
set(COEX_MANIFEST ${COEX_DIR}/coex.module)
set(COEX_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libcoex.a)
file(GLOB COEX_SOURCES ${COEX_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${COEX_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${COEX_MANIFEST}
            -o ${COEX_BIN}
    DEPENDS logosc ${COEX_MANIFEST} ${COEX_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libcoex.a"
)
add_custom_target(coex_binary ALL DEPENDS ${COEX_BIN})

# Full same-pkg-same-name type coexistence: two SEPARATE modules (coex2a, coex2b)
# each declare `package widgets; pub struct Widget {...}` with DIFFERENT layouts
# + a Box<Widget> generic, a method, a static. A consumer picks one via
# `use widgets from <module>`; BOTH archives link into the same binary without
# symbol collision (type identity is module-qualified). coex_from_a→7, coex_from_b→9.
set(COEX2A_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/coex2a/coex2a.module)
set(COEX2B_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/coex2b/coex2b.module)
set(COEX2A_BIN ${CMAKE_CURRENT_BINARY_DIR}/libcoex2a.a)
set(COEX2B_BIN ${CMAKE_CURRENT_BINARY_DIR}/libcoex2b.a)
file(GLOB COEX2A_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/coex2a/*.logos)
file(GLOB COEX2B_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/coex2b/*.logos)
add_custom_command(
    OUTPUT  ${COEX2A_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${COEX2A_MANIFEST} -L ${STDLIB_BIN_DIR} -o ${COEX2A_BIN}
    DEPENDS logosc ${COEX2A_MANIFEST} ${COEX2A_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libcoex2a.a")
add_custom_command(
    OUTPUT  ${COEX2B_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${COEX2B_MANIFEST} -L ${STDLIB_BIN_DIR} -o ${COEX2B_BIN}
    DEPENDS logosc ${COEX2B_MANIFEST} ${COEX2B_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libcoex2b.a")
add_custom_target(coex2_binary ALL DEPENDS ${COEX2A_BIN} ${COEX2B_BIN})

# Build liblazy_pkg.a from the lazy-mode fixture (Phase 6 multi-arena IR).
# Manifest has `lowering lazy`, so the archive ships only parsed AST
# (.wr0 member; no .o, no LIR blob). Consumer compile lowers locally.
set(LAZY_PKG_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/lazy_pkg)
set(LAZY_PKG_MANIFEST ${LAZY_PKG_DIR}/lazy_pkg.module)
set(LAZY_PKG_BIN      ${CMAKE_CURRENT_BINARY_DIR}/liblazy_pkg.a)
file(GLOB LAZY_PKG_SOURCES ${LAZY_PKG_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${LAZY_PKG_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${LAZY_PKG_MANIFEST}
            -o ${LAZY_PKG_BIN}
    # Phase 4 transition: lazy archives bake transitive stdlib package
    # names into their .wr0 — when stdlib renames a package (e.g. ops →
    # logos.lang.ops), the stale name in this archive collides with the
    # fresh name at consumer sema time. Add STDLIB_SOURCES so any stdlib
    # change triggers rebuild. Phase 7 cleanup removes the need.
    DEPENDS logosc ${LAZY_PKG_MANIFEST} ${LAZY_PKG_SOURCES} ${STDLIB_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: liblazy_pkg.a"
)
add_custom_target(lazy_pkg_binary ALL DEPENDS ${LAZY_PKG_BIN})

# Generic #[self_describing] DST module fixture: the emit-module build must
# clone the full DstRef-dispatched method chain (private helpers included) —
# regression for the $M-less named-call enqueue key (module-mode-only class).
set(SD_DST_MOD_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/sd_dst_mod)
set(SD_DST_MOD_MANIFEST ${SD_DST_MOD_DIR}/sd_dst_mod.module)
set(SD_DST_MOD_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libsd_dst_mod.a)
file(GLOB SD_DST_MOD_SOURCES ${SD_DST_MOD_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${SD_DST_MOD_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${SD_DST_MOD_MANIFEST}
            -o ${SD_DST_MOD_BIN}
    # BUILT stdlib archives, not just sources: the fixture imports
    # logos.lang.marker, so ninja must order it after the stdlib layers
    # (the sources-only dep raced on CI — the coex2 fixtures are the model).
    DEPENDS logosc ${SD_DST_MOD_MANIFEST} ${SD_DST_MOD_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libsd_dst_mod.a"
)
add_custom_target(sd_dst_mod_binary ALL DEPENDS ${SD_DST_MOD_BIN})

# Cross-module `container` fixture (ADR 0020): the declaration + backing type
# live in libctr_mod.a; a consumer re-registers the CONTAINER_DEF_DONE node
# and queries the generated projection across the module boundary.
set(CTR_MOD_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/ctr_mod)
set(CTR_MOD_MANIFEST ${CTR_MOD_DIR}/ctr_mod.module)
set(CTR_MOD_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libctr_mod.a)
file(GLOB CTR_MOD_SOURCES ${CTR_MOD_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${CTR_MOD_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${CTR_MOD_MANIFEST}
            -o ${CTR_MOD_BIN}
    DEPENDS logosc ${CTR_MOD_MANIFEST} ${CTR_MOD_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libctr_mod.a"
)
add_custom_target(ctr_mod_binary ALL DEPENDS ${CTR_MOD_BIN})

# Lazy→lazy chain fixture: liblazy_lower.a + liblazy_upper.a (both lazy).
# Exercises the case where a lazy module's `use` resolves to another
# lazy archive — both ASTs must reach the consumer's sema as user-code
# equivalent.
set(LAZY_CHAIN_DIR        ${CMAKE_CURRENT_SOURCE_DIR}/lazy_chain)
set(LAZY_LOWER_MANIFEST   ${LAZY_CHAIN_DIR}/lazy_lower.module)
set(LAZY_LOWER_BIN        ${CMAKE_CURRENT_BINARY_DIR}/liblazy_lower.a)
set(LAZY_UPPER_MANIFEST   ${LAZY_CHAIN_DIR}/lazy_upper.module)
set(LAZY_UPPER_BIN        ${CMAKE_CURRENT_BINARY_DIR}/liblazy_upper.a)
file(GLOB LAZY_LOWER_SOURCES ${LAZY_CHAIN_DIR}/lower/*.logos)
file(GLOB LAZY_UPPER_SOURCES ${LAZY_CHAIN_DIR}/upper/*.logos)
add_custom_command(
    OUTPUT  ${LAZY_LOWER_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${LAZY_LOWER_MANIFEST}
            -o ${LAZY_LOWER_BIN}
    DEPENDS logosc ${LAZY_LOWER_MANIFEST} ${LAZY_LOWER_SOURCES} ${STDLIB_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: liblazy_lower.a"
)
add_custom_command(
    OUTPUT  ${LAZY_UPPER_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${LAZY_UPPER_MANIFEST}
            -o ${LAZY_UPPER_BIN}
            -l ${LAZY_LOWER_BIN}
    DEPENDS logosc ${LAZY_UPPER_MANIFEST} ${LAZY_UPPER_SOURCES} ${LAZY_LOWER_BIN} ${STDLIB_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: liblazy_upper.a"
)
add_custom_target(lazy_chain_binary ALL DEPENDS ${LAZY_LOWER_BIN} ${LAZY_UPPER_BIN})

# Three-layer fixture (Phase 1 of the three-layer stdlib split): builds
# liblow.a → libmid.a (manifest `depends low`) → libhi.a (depends mid + low).
# Validates the manifest.depends → emit_module → load_modules wiring.
# The mid and hi manifests carry no `-l` flags — depends resolution alone
# must locate liblow.a / libmid.a in the build directory.
set(THREE_LAYER_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/three_layer)
set(THREE_LAYER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(THREE_LAYER_LOW_MANIFEST ${THREE_LAYER_DIR}/low.module)
set(THREE_LAYER_MID_MANIFEST ${THREE_LAYER_DIR}/mid.module)
set(THREE_LAYER_HI_MANIFEST  ${THREE_LAYER_DIR}/hi.module)
set(THREE_LAYER_LOW_BIN ${THREE_LAYER_OUT_DIR}/liblow.a)
set(THREE_LAYER_MID_BIN ${THREE_LAYER_OUT_DIR}/libmid.a)
set(THREE_LAYER_HI_BIN  ${THREE_LAYER_OUT_DIR}/libhi.a)
file(GLOB THREE_LAYER_LOW_SOURCES ${THREE_LAYER_DIR}/low/*.logos)
file(GLOB THREE_LAYER_MID_SOURCES ${THREE_LAYER_DIR}/mid/*.logos)
file(GLOB THREE_LAYER_HI_SOURCES  ${THREE_LAYER_DIR}/hi/*.logos)
add_custom_command(
    OUTPUT  ${THREE_LAYER_LOW_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${THREE_LAYER_LOW_MANIFEST}
            -o ${THREE_LAYER_LOW_BIN}
    DEPENDS logosc ${THREE_LAYER_LOW_MANIFEST} ${THREE_LAYER_LOW_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: liblow.a (three_layer)"
)
add_custom_command(
    OUTPUT  ${THREE_LAYER_MID_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${THREE_LAYER_MID_MANIFEST}
            -L ${THREE_LAYER_OUT_DIR}
            -o ${THREE_LAYER_MID_BIN}
    DEPENDS logosc ${THREE_LAYER_MID_MANIFEST} ${THREE_LAYER_MID_SOURCES} ${THREE_LAYER_LOW_BIN} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libmid.a (three_layer; depends low)"
)
add_custom_command(
    OUTPUT  ${THREE_LAYER_HI_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${THREE_LAYER_HI_MANIFEST}
            -L ${THREE_LAYER_OUT_DIR}
            -o ${THREE_LAYER_HI_BIN}
    DEPENDS logosc ${THREE_LAYER_HI_MANIFEST} ${THREE_LAYER_HI_SOURCES} ${THREE_LAYER_MID_BIN} ${THREE_LAYER_LOW_BIN} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libhi.a (three_layer; depends mid + low)"
)
add_custom_target(three_layer_binary ALL DEPENDS
    ${THREE_LAYER_LOW_BIN} ${THREE_LAYER_MID_BIN} ${THREE_LAYER_HI_BIN})

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
    writ_string
    writ_typetag
    writ_objectmap
    writ_document
    writ_stringify
    writ_ex_arena
    writ_ex_containers
    writ_parse
    writ_round_trip
    writ_stringbuf_grow
    writ_anyval_methods
    writ_ctr_traversal
    writ_builder
    writ_registry_lookup
    writ_traits_full
    writ_traits_bugfix
    writ_scalar
    writ_buffer_i64
    writ_buffer_string
    writ_string_view_interop
    writ_zone_seal
    writ_memholder_rc
    writ_check
    writ_import_export
    writ_hbs
    writ_lit_parse
    assoc_const_writ_static
    writ_sdn_lit
    writ_typed_arr_lit
    writ_typed_map_lit
    writ_capture_scalar
    writ_capture_str
    writ_capture_f64
    writ_param_roundtrip
    writ_as_array
    writ_as_map
    writ_typed_array_variants
    writ_as_array_variants
    writ_as_map_variants
    writ_ctr_view
    writ_typed_array_all_elems
    writ_typed_map_all_keys
    writ_typed_container_clone
    writ_typed_container_equal
    writ_view_from_static writ_typed_push writ_list_comp_basic writ_list_comp_edge writ_map_comp_basic writ_map_comp_multi writ_comp_coerce writ_match_scalar writ_match_or writ_match_str writ_match_map writ_match_arr writ_match_nested writ_match_bind writ_match_rest writ_match_typed writ_match_or_struct array_i32_generic_api array_generic_push_grow writ_typed_array_grow
    fiber_basic fiber_yield fiber_future
    fiber_thread_basic fiber_thread_future
    fiber_chan_basic fiber_latch_basic fls_tls_basic
    auto_trait_send_scalar auto_trait_send_struct auto_trait_unsafe_impl auto_trait_generic_propagation
    blanket_impl_unbounded blanket_impl_assoc_eq instantiate_decl
    vec_full hashset_basic vecdeque_basic btreemap_basic
    atomic_basic mutex_basic
    box_basic box_drop_raii rc_basic rc_drop_inner arc_basic bytes_basic box_dyn_trait_basic
    io_uring_basic
    fs_basic env_basic io_print_basic panic_basic pipe_basic io_read_write_trait io_writev_basic
    path_basic process_basic net_basic
    math_basic random_basic thread_basic bufio_basic bufio_generic_reader
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
        use_from_module
        wrapping_arith
        format_basic format_types variadic_format debug_primitives meta_has_trait
        hello stdlib_demo option_chain try_option mem_take_basic if_while_let while_let_chain closure_in_struct_field string_basic std_hash_basic std_cmp_basic annotation_has_get
        std_hashmap_basic std_hashmap_full std_hashmap_iter std_hashmap_multi_k
        iter_basic
        iter_adapters
        copy_let_independence
        for_iter_range
        list_comp_basic
        map_comp_basic
        reflect_api
        reflect_annotated_auto)
    set(LOCAL_PUBLIB_USERS pub_module_internal_use pub_module_type_internal pub_cross_package pkg_multifile pub_reexport pub_enum_trait_cross_pkg pub_static_cross_module pub_dyn_cross_module wql_wref_field_pkg)
    # B-mv-01 fixture: two packages defining same-base+sig fns coexist.
    set(LOCAL_COEX_USERS cross_pkg_coexistence)
    # ADR 0016: cross-module mapping fusion against libwql_map_lib.a
    set(LOCAL_WQLMAP_USERS wql_mapping_cross_module_e2e)
    # Full same-pkg-same-name TYPE coexistence: each consumer `use widgets from
    # <module>` selects one of two modules that both declare `widgets::Widget`;
    # both archives link without collision (module-qualified type identity).
    set(LOCAL_COEX2_USERS coex_from_a coex_from_b)
    # Phase 6 (multi-arena IR) lazy-mode fixture: archive ships AST only.
    set(LOCAL_LAZY_PKG_USERS lazy_pkg_basic)
    # Generic self-describing DST from a module archive (fresh instantiation).
    set(LOCAL_SD_DST_MOD_USERS sd_dst_module_methods)
    # ADR 0020: container declared in a binary module, queried by a consumer.
    set(LOCAL_CTR_MOD_USERS container_item_from_module)
    # Phase 6 lazy→lazy chain: consumer pulls upper (lazy) which pulls
    # lower (also lazy). Both archives must reach sema as user code.
    set(LOCAL_LAZY_CHAIN_USERS lazy_pkg_chain)
    # Phase 1 of three-layer stdlib split: consumer pulls hi (which
    # depends mid + low via manifest, both resolved by emit_module).
    set(LOCAL_THREE_LAYER_USERS three_layer_chain)
    set(LOCAL_METAPROG_PUBLIB_USERS metacall_item_use_inherit)
    # Tests that use std.compiler.metaprog (excluded from binary stdlib;
    # metaprog_stubs in libstdlib_rt.a satisfies the link).
    set(LOCAL_METAPROG_USERS assembler_last_wins assembler_method_set wstatic_const_generic_uid wstatic_const_generic_extract wstatic_const_generic_alias showcase_type_metaprog writ_type_lit_roundtrip derive_debug_e2e derive_debug_enum derive_debug_generic item_list_splice metacall_item_list metaprog_gensym_smoke metaprog_type_to_ast multi_derive_e2e quote_item_smoke quote_item_antiquot quote_item_nested_antiquot quote_item_exprblob_splice quote_item_static_call quote_nested_repeat metacall_expr_blob metacall_item_smoke metacall_item_generic_smoke metacall_item_odr_dedup metacall_item_instantiate quote_expr_smoke quote_expr_antiquot quote_expr_repeat_andand quote_expr_repeat_comma quote_expr_repeat_slice quote_expr_subblob slice6_field_accessors meta_pack_basic meta_pack_search meta_kind_preds meta_field_iter meta_fold_i64 meta_field_byname meta_type_methods meta_kind_name quote_ty_smoke meta_args_count meta_tuple_intrinsics meta_has_trait meta_typelist meta_reify_type meta_apply meta_quote_ty_antiquot meta_quote_ty_tup_arr meta_variant_intrinsics meta_generic_handle meta_type_align meta_has_trait_of meta_quote_ty_pack meta_template_handle meta_typelist_a_side meta_fold_to_type meta_template_of_smoke meta_template_of_typed template_item_handle_smoke meta_transform meta_filter meta_splice_call meta_schema_recurse meta_schema_code meta_dyn_from_code quote_struct_lit_cursor derive_clone_quote derive_clone_stdlib meta_type_hash iter_cycle test_harness_coretest_iter_cycle data_revive_dyn token_macro_item_named_resource token_macro_item_params_resource)
    set(LOCAL_WRIT_USERS writ_type_lit_parse writ_lit_parse datatype_zone_basic datatype_zone_ref datatype_zone_ergonomic datatype_own datatype_relptr anyval_basic writ_containers writ_string writ_typetag writ_objectmap writ_document writ_stringify writ_ex_arena writ_ex_containers writ_parse writ_round_trip writ_stringbuf_grow writ_anyval_methods writ_ctr_traversal writ_float_parse writ_escape_strings writ_builder writ_registry_lookup writ_traits_full writ_traits_bugfix writ_scalar writ_decimal_methods writ_decimal_limits_errors writ_decimal_f64 writ_compactify writ_clone writ_buffer_i64 writ_buffer_string writ_string_view_interop datatag_typecode tag_system_basic tag_dispatch_table data_plain_check tag_dispatch_call tag_dispatch_tier2 tag_dispatch_sibling_concrete writ_zone_seal writ_memholder_rc podref_buffer mem_holder_custom_destroyer array_i32_polymorphic writ_parse_typed_containers writ_typed_array_materialize writ_typed_array_element_type_errors writ_typed_datatype_values writ_typed_datatype_values_errors writ_typed_datatype_trait_coverage writ_typed_datatype_params_overflow writ_typed_datatype_empty_params_canonical writ_typed_datatype_empty_params_small_zone writ_typed_datatype_params_trailing_comma_errors writ_typed_decimal_validation_pass writ_typed_decimal_validation_errors writ_typed_map_default_materialize writ_typed_map_i32_materialize writ_typed_map_key_type_errors writ_typed_materialization_errors array_relptr_strings writ_check writ_import_export writ_hbs dec_arithmetic dec_boundary dec_deep dec_edge_cases writ_sdn_lit writ_typed_arr_lit writ_typed_map_lit writ_typed_array_all_elems writ_typed_map_all_keys writ_typed_container_clone writ_typed_container_equal writ_capture_scalar writ_capture_str writ_capture_f64 writ_param_roundtrip writ_as_array writ_as_map writ_typed_array_variants writ_as_array_variants writ_as_map_variants writ_ctr_view writ_view_from_static writ_typed_push writ_list_comp_basic writ_list_comp_edge writ_map_comp_basic writ_map_comp_multi writ_comp_coerce writ_match_scalar writ_match_or writ_match_str writ_match_map writ_match_arr writ_match_nested writ_match_bind writ_match_rest writ_match_typed writ_match_or_struct array_i32_generic_api array_generic_push_grow writ_typed_array_grow reflect_basic meta_struct meta_trait genos_reflect metacall_writ_static metacall_writ fiber_basic fiber_yield fiber_future fiber_thread_basic fiber_thread_future fiber_chan_basic fiber_latch_basic fls_tls_basic fiber_reactor_basic fiber_sleep_ms auto_trait_send_scalar auto_trait_send_struct auto_trait_unsafe_impl auto_trait_generic_propagation assoc_const_writ_static
    vec_full hashset_basic vecdeque_basic btreemap_basic
    atomic_basic mutex_basic
    box_basic rc_basic arc_basic bytes_basic box_dyn_trait_basic
    chain_field_write chain_field_write_deep chain_field_write_scalar_named
    field_index_write_generic_tuple index_write_unified field_write_unified
    meta_traits_basic
    utf8_basic
    time_basic
    io_uring_basic
    fs_basic env_basic io_print_basic panic_basic pipe_basic io_read_write_trait io_writev_basic
    path_basic process_basic net_basic
    math_basic random_basic thread_basic bufio_basic bufio_generic_reader
    signal_basic os_basic
    blanket_impl_sibling_bound_resolution blanket_chain_two_hops blanket_chain_assoc_eq blanket_chain_subst generic_chain_write_through_ptr
    generic_ref_registry_shape generic_ref_registry_dispatch path_expr_multi_step wstatic_generic_type_arg
    hex_basic base64_basic url_basic url_parse_basic http_types_basic http_parse_basic http_parse_corpus http_golden_suite http_parse_stream http_serialize_stream http_service_basic http_serve_bounded http_keepalive http_workers_basic http_chunked_body_helper http_chunked http_chunked_reader http_framing_readers http_roundtrip json_basic log_basic args_basic crypto_basic testing_basic csv_basic datetime_basic regex_basic)

    # Post-C2 / C3 / C4: logosc auto-discovers liblstdlib.a via its
    # baked-in argv[0]-relative path; metaprog modules ship as AST-only
    # `.writ0` members of the same archive, so tests no longer need
    # any -I/-L for the system stdlib. The remaining -I lookups are for
    # local test fixtures (cross-pkg coexistence — source-only).
    # pub_lib is built as a binary archive and consumed via -l (matches
    # the user-facing workflow for shipping libraries).
    if (BASE IN_LIST LOCAL_METAPROG_PUBLIB_USERS)
        set(_extra -l ${PUB_LIB_BIN})
    elseif (BASE IN_LIST LOCAL_PUBLIB_USERS)
        set(_extra -l ${PUB_LIB_BIN})
    elseif (BASE IN_LIST LOCAL_WQLMAP_USERS)
        set(_extra -l ${WQL_MAP_LIB_BIN})
    elseif (BASE IN_LIST LOCAL_COEX_USERS)
        set(_extra -l ${COEX_BIN})
    elseif (BASE IN_LIST LOCAL_COEX2_USERS)
        set(_extra -l ${COEX2A_BIN} -l ${COEX2B_BIN})
    elseif (BASE IN_LIST LOCAL_LAZY_PKG_USERS)
        set(_extra -l ${LAZY_PKG_BIN})
    elseif (BASE IN_LIST LOCAL_SD_DST_MOD_USERS)
        set(_extra -l ${SD_DST_MOD_BIN})
    elseif (BASE IN_LIST LOCAL_CTR_MOD_USERS)
        set(_extra -l ${CTR_MOD_BIN})
    elseif (BASE IN_LIST LOCAL_LAZY_CHAIN_USERS)
        set(_extra -l ${LAZY_LOWER_BIN} -l ${LAZY_UPPER_BIN})
    elseif (BASE IN_LIST LOCAL_THREE_LAYER_USERS)
        set(_extra -l ${THREE_LAYER_HI_BIN} -l ${THREE_LAYER_MID_BIN} -l ${THREE_LAYER_LOW_BIN})
    else()
        set(_extra "")
    endif()

    # `--test` harness tests: file name prefix `test_harness_` triggers
    # logosc's --test mode, which synthesises a main() that runs every
    # #[test] fn under panic-recovery.
    if (BASE MATCHES "^test_harness_")
        list(APPEND _extra --test)
    endif()

    set(${OUT_VAR} "${_extra}" PARENT_SCOPE)
endfunction()

function(logos_fail_extra_args BASE OUT_VAR)
    set(FAIL_PUBLIB_USERS pub_module_cross_module pub_module_type_cross_module pub_call_private pub_private_field_access enum_private_cross_pkg trait_private_cross_pkg)
    set(FAIL_METAPROG_USERS metacall_item_odr_conflict)
    set(FAIL_COEX_USERS cross_pkg_ambiguous_call)
    set(FAIL_WQLMAP_USERS wql_mapping_cross_module_priv_fail wql_mapping_cross_module_modvis_fail)
    if (BASE IN_LIST FAIL_PUBLIB_USERS)
        set(_extra -l ${PUB_LIB_BIN})
    elseif (BASE IN_LIST FAIL_WQLMAP_USERS)
        set(_extra -l ${WQL_MAP_LIB_BIN})
    elseif (BASE IN_LIST FAIL_METAPROG_USERS)
        set(_extra "")  # post-C4: metaprog is in liblstdlib.a as AST-only
    elseif (BASE IN_LIST FAIL_COEX_USERS)
        set(_extra -l ${COEX_BIN})
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
        ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
    )
endfunction()

# ── Pass tests ────────────────────────────────────────────────────────────────
# Both .logos and .expected live in tests/logos/pass/ (native suite).
# Imported rust-derived tests live in tests/imported/{pass,fail}/<category>/
# and are discovered with GLOB_RECURSE under their category subtrees.

file(GLOB LOCAL_PASS_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/pass/*.expected)
file(GLOB_RECURSE IMPORTED_PASS_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/tests/imported/pass/*.expected)
list(APPEND LOCAL_PASS_EXPECTED ${IMPORTED_PASS_EXPECTED})
list(SORT LOCAL_PASS_EXPECTED)

# ── Fail tests ────────────────────────────────────────────────────────────────
# Each .expected file in fail/ matches a .logos file in the same directory.

file(GLOB FAIL_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/fail/*.expected)
file(GLOB_RECURSE IMPORTED_FAIL_EXPECTED CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/tests/imported/fail/*.expected)
list(APPEND FAIL_EXPECTED ${IMPORTED_FAIL_EXPECTED})
list(SORT FAIL_EXPECTED)

# Register tests in suite order so plain `ctest` runs them in grouped batches.
foreach(SUITE IN LISTS LOGOS_SUITE_ORDER)
    foreach(EXP_FILE IN LISTS LOCAL_PASS_EXPECTED)
        get_filename_component(BASE ${EXP_FILE} NAME_WE)
        string(REGEX REPLACE "\\.expected$" ".logos" LOGOS_FILE ${EXP_FILE})

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
        string(REGEX REPLACE "\\.expected$" ".logos" LOGOS_FILE ${EXP_FILE})

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
        )
        set_tests_properties(${_test_name} PROPERTIES
            TIMEOUT 30
            LABELS  "logos;ir_snapshot;suite_ir"
            ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
        )
    endforeach()
else()
    message(STATUS "logos tests: FileCheck not found — skipping IR snapshot suite")
endif()

# ── Spec conformance tests ────────────────────────────────────────────────────
# tools/spec-extract generates tests/spec/{pass,fail}/*.logos (+ .expected), each
# EXECUTING the language-spec rules it confirms (// @rule <id> markers). Run via
# the same run_test.sh harness — these lock the extracted spec against drift: if
# the compiler's behaviour changes away from a confirmed rule, the suite goes red.
# Pure-language + stdlib-`use` tests resolve via logosc's baked search path (no
# per-test -I); LOGOS_LIB_DIR supplies the link archives, same as the native suite.
file(GLOB SPEC_PASS_EXPECTED CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/spec/pass/*.expected)
file(GLOB SPEC_FAIL_EXPECTED CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/spec/fail/*.expected)
list(SORT SPEC_PASS_EXPECTED)
list(SORT SPEC_FAIL_EXPECTED)

foreach(EXP_FILE IN LISTS SPEC_PASS_EXPECTED)
    get_filename_component(BASE ${EXP_FILE} NAME_WE)
    string(REGEX REPLACE "\\.expected$" ".logos" LOGOS_FILE ${EXP_FILE})
    if (NOT EXISTS ${LOGOS_FILE})
        message(WARNING "spec test: no .logos for ${EXP_FILE}, skipping")
        continue()
    endif()
    set(_test_name "logos_25_spec_pass_${BASE}")
    add_test(NAME ${_test_name}
        COMMAND ${TEST_RUNNER} pass $<TARGET_FILE:logosc> ${LOGOS_FILE} ${EXP_FILE})
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT 30 LABELS "logos;pass;suite_spec"
        ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}")
endforeach()

foreach(EXP_FILE IN LISTS SPEC_FAIL_EXPECTED)
    get_filename_component(BASE ${EXP_FILE} NAME_WE)
    string(REGEX REPLACE "\\.expected$" ".logos" LOGOS_FILE ${EXP_FILE})
    if (NOT EXISTS ${LOGOS_FILE})
        message(WARNING "spec test: no .logos for ${EXP_FILE}, skipping")
        continue()
    endif()
    set(_test_name "logos_25_spec_fail_${BASE}")
    add_test(NAME ${_test_name}
        COMMAND ${TEST_RUNNER} fail $<TARGET_FILE:logosc> ${LOGOS_FILE} ${EXP_FILE})
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT 15 LABELS "logos;fail;suite_spec"
        ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}")
endforeach()

# Lint: inline `LogosTypeBuilder` Struct/Enum/ZonedStruct sites outside
# sema_impl.hpp.  Closes the ad-hoc-typebuilder cluster (Meta-Sprint M0.4).
add_test(
    NAME logos_08_lint_inline_typebuilder
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check_typebuilder_lint.sh
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(logos_08_lint_inline_typebuilder PROPERTIES
    TIMEOUT 10
    LABELS  "logos;lint;suite_lint"
)

# Diagnostics: --diag-format=json + structured exit codes (B0.2).
add_test(
    NAME logos_09_diag_json_format
    COMMAND ${CMAKE_SOURCE_DIR}/tests/diag/json_format.sh
            $<TARGET_FILE:logosc>
)
set_tests_properties(logos_09_diag_json_format PROPERTIES
    TIMEOUT 30
    LABELS  "logos;diag;suite_diag"
)

# lforge end-to-end smoke: build, run, clean a hello-world fixture (B0.5).
if (TARGET lforge)
    add_test(
        NAME logos_10_lforge_smoke
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/smoke.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_10_lforge_smoke PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.1: multi-target builds (lib + bin with deps), topo-sort, --release.
    add_test(
        NAME logos_11_lforge_multitarget
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/multitarget.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_11_lforge_multitarget PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.5: lforge test discovers tests/*.logos, builds + runs each.
    add_test(
        NAME logos_12_lforge_test_cmd
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/test_cmd.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_12_lforge_test_cmd PROPERTIES
        TIMEOUT 90
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.2: mtime-based incremental rebuild.
    add_test(
        NAME logos_13_lforge_incremental
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/incremental.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_13_lforge_incremental PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.6: lforge install <prefix> places artifacts under <prefix>/{bin,lib}.
    add_test(
        NAME logos_14_lforge_install
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/install.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_14_lforge_install PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # `lforge build --dump-metacall` writes metafn-emitted ASTs as Logos source.
    add_test(
        NAME logos_14_lforge_dump_metacall
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/dump_metacall.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_14_lforge_dump_metacall PROPERTIES
        TIMEOUT 90
        LABELS  "logos;lforge;suite_lforge"
    )
endif()

# B1.7: logosc --only-file <path> per-file emit.
add_test(
    NAME logos_15_emit_file
    COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/emit_file.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_15_emit_file PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lforge;suite_lforge"
)

# ADR 0014 slice-1: logosc --emit-docs writes a .docwr doc-facts container.
add_test(
    NAME logos_15_emit_docs
    COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/emit_docs.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_15_emit_docs PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lforge;suite_lforge"
)

# ADR 0014 slice-2: tools/docgen resolves a .docwr into docs.json (impl edges →
# implementors/implements cross-refs + undocumented surface).
add_test(
    NAME logos_15_doc_json
    COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/doc_json.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_15_doc_json PROPERTIES
    TIMEOUT 90
    LABELS  "logos;lforge;suite_lforge"
)

# B1.3: lforge fans out per-file lib compilation in parallel + per-file
# incremental rebuild.
if (TARGET lforge)
    add_test(
        NAME logos_16_lforge_parallel
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/parallel.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_16_lforge_parallel PROPERTIES
        TIMEOUT 90
        LABELS  "logos;lforge;suite_lforge"
    )

    # ADR 0014 slice-3: `lforge doc` builds libs, --emit-docs, resolves docs.json.
    add_test(
        NAME logos_16_lforge_doc
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/doc_cmd.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_16_lforge_doc PROPERTIES
        TIMEOUT 120
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.8: lib targets accept C and asm sources alongside Logos files.
    add_test(
        NAME logos_17_lforge_c_asm
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/c_asm.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_17_lforge_c_asm PROPERTIES
        TIMEOUT 90
        LABELS  "logos;lforge;suite_lforge"
    )

    # B1.9: dogfood — build stdlib_rt + stdlib_fibers via lforge and verify
    # the resulting archives match cmake's member set + export expected symbols.
    add_test(
        NAME logos_18_lforge_dogfood_rt
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/dogfood_rt.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
                ${CMAKE_SOURCE_DIR}/stdlib/rt
    )
    set_tests_properties(logos_18_lforge_dogfood_rt PROPERTIES
        TIMEOUT 120
        LABELS  "logos;lforge;suite_lforge"
    )

    # B2: project-level external deps via local paths.
    add_test(
        NAME logos_19_lforge_external_dep
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/external_dep.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_19_lforge_external_dep PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B2.5: project-level external deps via git URLs (uses a local file://
    # repo created in tmp; no network required).
    add_test(
        NAME logos_20_lforge_git_dep
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/git_dep.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_20_lforge_git_dep PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B3a: transitive external deps -- a closure of local-path projects
    # where mid pulls leaf transitively.
    add_test(
        NAME logos_21_lforge_transitive_dep
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/transitive_dep.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_21_lforge_transitive_dep PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B3a: lockfile records git pins; second build uses them without
    # ls-remote; manifest tag change forces re-resolve.
    add_test(
        NAME logos_22_lforge_lockfile
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/lockfile.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_22_lforge_lockfile PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B3b: MVS conflict detection + `lforge update` re-resolves.
    add_test(
        NAME logos_23_lforge_mvs_update
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/mvs_update.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_23_lforge_mvs_update PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B4: content-addressed build cache shared across consumers.
    add_test(
        NAME logos_24_lforge_build_cache
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/build_cache.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_24_lforge_build_cache PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )

    # B5: replace: directive + requires_logos: ABI floor.
    add_test(
        NAME logos_25_lforge_replace_and_floor
        COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/replace_and_floor.sh
                ${CMAKE_BINARY_DIR}/bin/lforge
                $<TARGET_FILE:logosc>
                ${LOGOS_BUILD_LIB_DIR}
    )
    set_tests_properties(logos_25_lforge_replace_and_floor PROPERTIES
        TIMEOUT 60
        LABELS  "logos;lforge;suite_lforge"
    )
endif()
