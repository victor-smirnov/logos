# Copyright 2026 Victor Smirnov
# Logos compiler test suite — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

# ── EVERY GATE IN THIS FILE DECLARES ITS OWN TIER ────────────────────────────
# test-levels.sh no longer knows any gate NAMES: it selects the per-commit gates
# tier with `-L '^tier_commit$'`. So a gate that declares no tier would run in no
# tier at all while still looking registered — the exact silent-drop this
# replaced. This call installs the audit that makes that a configure failure.
# See cmake/LogosTestTiers.cmake.
logos_require_tier_labels()

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

# ── libub_boundary.a — THE ARCHIVE BOUNDARY THAT MAKES THE FREEZE CANARY BITE ─
# `noalias`/`readonly` on a parameter is a claim made TO LLVM, and LLVM can only
# act on it while the call is still a call. In ONE translation unit the mutator
# inlines into its caller, the parameter and its attributes vanish, and the
# optimizer sees the real stores — which is why the old single-file
# interior_mut_freeze_canary stayed green at -O0..-O3 even with the Freeze gate
# forced open (MEASURED 2026-08-04). Compiling the mutators separately keeps the
# call a call.
#
# ⚠ THE `-O2` IS THE TEST, NOT A SETTING. The read → mutate → read sequence has
# to survive optimization INSIDE this archive with the attribute still attached;
# at -O0 nothing consults it and the fixture asserts nothing. Because the
# miscompile is baked in HERE, the consuming test bites at logosc's default -O0,
# which is what the corpus compiles at. Control revert, both codes observed:
# gate forced open -> the consumer exits 1 (and 3 with probe 1 removed); gate
# restored -> exits 0.
#
# ⚠ NO `depends` LINE IN THE MANIFEST, deliberately, matching pub_lib: the
# warning logosc prints says dependency bodies are RECOMPILED from their
# archived ASTs rather than linked, and that is precisely what this fixture
# needs — Cell::get/set must be monomorphised and inlined into THIS archive at
# -O2 for the fold to be reachable at all.
set(UB_BOUNDARY_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/ub_boundary)
set(UB_BOUNDARY_MANIFEST ${UB_BOUNDARY_DIR}/ub_boundary.module)
set(UB_BOUNDARY_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libub_boundary.a)
file(GLOB UB_BOUNDARY_SOURCES ${UB_BOUNDARY_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${UB_BOUNDARY_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${UB_BOUNDARY_MANIFEST} -O2 -L ${STDLIB_BIN_DIR}
            -o ${UB_BOUNDARY_BIN}
    DEPENDS logosc ${UB_BOUNDARY_MANIFEST} ${UB_BOUNDARY_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libub_boundary.a (-O2, Freeze-predicate boundary)"
)
add_custom_target(ub_boundary_binary ALL DEPENDS ${UB_BOUNDARY_BIN})

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

# ADR 0021 Phase 4a crown gate: generic container alias (PMapCfg<K,V> +
# PMap<K,V> = CtrClass<PMapCfg<K,V>>) declared in a binary module; the
# consumer instantiates PMap<u64,u64> cross-module through the mono→factory
# demand path (the map_consumer wall).
set(PMAP_MOD_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/pmap_mod)
set(PMAP_MOD_MANIFEST ${PMAP_MOD_DIR}/pmap_mod.module)
set(PMAP_MOD_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libpmap_mod.a)
file(GLOB PMAP_MOD_SOURCES ${PMAP_MOD_DIR}/*.logos)
add_custom_command(
    OUTPUT  ${PMAP_MOD_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${PMAP_MOD_MANIFEST}
            -o ${PMAP_MOD_BIN}
    DEPENDS logosc ${PMAP_MOD_MANIFEST} ${PMAP_MOD_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libpmap_mod.a"
)
add_custom_target(pmap_mod_binary ALL DEPENDS ${PMAP_MOD_BIN})

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
    quote_expr_call_arg_shapes
    quote_expr_stmt_fold
    quote_item_exprblob_cursor
    any_downcast
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
    # G156-1 fixture: two packages of the SAME module declare same-named structs
    # (alpha::Ping / beta::Ping, different layouts) each used through Vec<Ping> —
    # the package fingerprint keeps the generic-instance symbols distinct.
    set(LOCAL_COEX_USERS cross_pkg_coexistence cross_pkg_type_coexistence cross_pkg_type_id_distinct cross_pkg_const_scoped)
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
    # ADR 0021 Phase 4a: generic container alias from a binary module,
    # instantiated cross-module through the mono→factory demand path.
    set(LOCAL_PMAP_MOD_USERS metaclass_pmap_from_module)
    # Phase 6 lazy→lazy chain: consumer pulls upper (lazy) which pulls
    # lower (also lazy). Both archives must reach sema as user code.
    set(LOCAL_LAZY_CHAIN_USERS lazy_pkg_chain)
    # Phase 1 of three-layer stdlib split: consumer pulls hi (which
    # depends mid + low via manifest, both resolved by emit_module).
    set(LOCAL_THREE_LAYER_USERS three_layer_chain)
    # The Freeze-predicate behavioural guard. Its mutators live in
    # libub_boundary.a (built at -O2) because a parameter attribute only has
    # teeth while the call is still a call — see the fixture's comment above.
    set(LOCAL_UB_BOUNDARY_USERS interior_mut_freeze_canary)
    set(LOCAL_METAPROG_PUBLIB_USERS metacall_item_use_inherit)
    # Tests that use std.compiler.metaprog (excluded from binary stdlib;
    # metaprog_stubs in libstdlib_rt.a satisfies the link).
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
    elseif (BASE IN_LIST LOCAL_UB_BOUNDARY_USERS)
        set(_extra -l ${UB_BOUNDARY_BIN})
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
    elseif (BASE IN_LIST LOCAL_PMAP_MOD_USERS)
        set(_extra -l ${PMAP_MOD_BIN})
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
    set(FAIL_COEX_USERS cross_pkg_ambiguous_call cross_pkg_const_ambiguous)
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
        # A test that FORGES A CONTAINER FAMILY runs the metaclass factory and
        # costs 6-9s alone against a suite median well under one. Under the full
        # `ctest -j12` those overshoot the 30s limit and fail as TIMEOUT — a
        # flake that reads exactly like a hang.
        #
        # DERIVED, not listed: a test forges a family iff it imports the
        # factory, so the rule asks the source rather than naming tests. The
        # first attempt here WAS a list of five names and the next full run
        # failed on `metaclass_*` tests that were not on it.
        #
        # ⚠ History worth keeping, because it cuts both ways. An external OCR
        # process was hogging the box during part of this, which is a real
        # confounder and the reason to check `top` before theorising about a
        # load-shaped flake. But the raise was reverted on that news and the very
        # next run — on a quiet box — timed out on `container_item_e2e` again.
        # The cost signal is real and independent of the confounder. 60s, not
        # 120: measured contention is ~4x, so this keeps ~7x headroom while
        # still catching a genuine hang.
        file(READ "${LOGOS_FILE}" _src)
        if (_src MATCHES "logos\\.lcm\\.canon\\.(metaclass|container_item)")
            set(_timeout 60)
        endif()
    else()
        logos_fail_extra_args(${BASE} EXTRA)
        set(_timeout 15)
    endif()

    # The Rust-derived conformance ports (tests/imported/) are 55% of the suite
    # but survivor-biased and largely duplicate the core-logos coverage. They
    # carry an extra `imported` label so the everyday loop can exclude them
    # (`ctest -LE imported`, and test-levels.sh L0-L4) while the full
    # conformance gate stays one `ctest -L imported` away — reversible, nothing
    # deleted. See docs/dev-checklist / the corpus-revision decision.
    # ── `corpus` IS WHAT EXEMPTS THESE FROM DECLARING A TIER ─────────────────
    # The tier canary (cmake/LogosTestTiers.cmake) demands exactly one tier
    # label per test, and the sampler-reachable corpus is the one population
    # that legitimately has none: it is selected by the per-level chunk regexes
    # over enumerated `.logos` files, never by a tier. The exemption is derived
    # from the REGISTRATION PATH — not from a name prefix, which is the listed
    # population the tier labels exist to retire.
    #
    # ⚠ THIS SAID "stamped HERE, at the single funnel every such test passes
    # through". MEASURED FALSE: `logos;corpus` is stamped at THREE sites — this
    # one and the two `suite_spec` loops below (the pass loop and the fail
    # loop), which together carry 608 of the 6815 corpus tests. The mechanism
    # survives the correction, because all three are loops over ENUMERATED
    # FILES rather than name prefixes, so the exemption is still derived. What
    # is false is "single", and a reader who trusts it will add a fourth site
    # believing the canary funnels everything past this line.
    # ⚠ Nothing constrains who may write `corpus`: any future test carrying
    # that label escapes the tier canary. That is the escape hatch's real
    # perimeter, and it is unguarded.
    set(_labels "logos;corpus;${MODE};suite_${SUITE}")
    if (LOGOS_FILE MATCHES "/tests/imported/")
        set(_labels "${_labels};imported")
    endif()

    # ── THE LAYOUT VERIFIER'S POPULATION IS THE CORPUS ────────────────────────
    # `verify_layout_engines()` compares five engines that size a value. Two of
    # them — sema's and mono's, the ones whose numbers become byte OFFSETS and
    # `malloc` sizes — are not reachable from the comparison site, so they RECORD
    # what they answered and the record is checked. Recording is behind
    # `LOGOS_VERIFY_LAYOUT`, and until now the only thing that set it was
    # `layout_engine_agreement_gate.sh`, over a lattice IT generates. The gate
    # imports nothing, so the answer it certified was about ~200 synthetic types
    # and said nothing whatever about the 1913 real programs.
    #
    # MEASURED 2026-08-01, all 1913 `pass/*.logos` compiled with the variable
    # set (23 need archives the sweep did not pass): of the remaining 1890, TWO
    # were red — and one of them, `option_ptr_wrapper_niche`, was a live
    # `sizeof` miscompile of every generic enum in a rendered context, sitting
    # behind an `.expected` the harness never compared.
    #
    # COST, MEASURED, three runs each on `rc_arc_dyn_dispatch`: 0.48/0.48/0.47s
    # without, 0.48/0.49/0.48s with. The verification rides a compile that is
    # already happening, so making the population the corpus is free and belongs
    # in EVERY tier (L0-L4) rather than in a separate heavy pass.
    #
    # PASS TESTS ONLY, and that exclusion is not cosmetic: a `fail` test compiles
    # an ill-typed program on purpose, `TypeVar`/`Error` residue reaching a
    # layout switch is the CORRECT behaviour there, and the resulting decline
    # would replace the diagnostic the test asserts on. The `fail` corpus is
    # excluded by kind, counted here as a kind, not silently.
    #
    # The one further exclusion is BY NAME and carries its ground:
    # `LAYOUT_DECLINE_LEDGER` below. It is enforced in both directions by
    # `logos_00_layout_decline_ledger`, so it can only shrink.
    set(_env "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}")
    if (MODE STREQUAL pass AND NOT BASE IN_LIST LAYOUT_DECLINE_LEDGER)
        list(APPEND _env "LOGOS_VERIFY_LAYOUT=1")
    endif()
    # The one exclusion from R2 (mlir-gen's self-diagnosis = compile failure).
    # The variable NAMES THE LEDGER; it does not grant the excuse — logosc asks
    # whether the program it is compiling is listed, so this can be handed to
    # EVERY test unconditionally. That is deliberate: the previous boolean form
    # made the excused set a hand-written list right here, i.e. a second copy of
    # a population that already exists as an artifact, and the copies drifted
    # (see the spec-corpus loop below, which measured exactly that). Population
    # derived from the artifact, not from whoever remembered to add a name.
    list(APPEND _env "LOGOS_MLIRGEN_BUG_LEDGER=${CMAKE_CURRENT_SOURCE_DIR}/mlir_gen_bug.ledger")

    add_test(
        NAME  ${_test_name}
        COMMAND ${TEST_RUNNER} ${MODE} $<TARGET_FILE:logosc>
                ${LOGOS_FILE} ${EXP_FILE} ${EXTRA}
    )
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT ${_timeout}
        LABELS  "${_labels}"
        ENVIRONMENT "${_env}"
    )
endfunction()

# ── THE REGISTRATION CENSUS ───────────────────────────────────────────────────
# Everything below registers tests by globbing `*.expected` and deriving the
# `.logos` from it. That asymmetry is the hole: an `.expected` with no `.logos`
# gets a `message(WARNING …)` a few lines down, while a `.logos` with no
# `.expected` gets NOTHING — no warning, no test, no trace. Such a file is
# indistinguishable from a passing one at every level of the suite, and
# `test-levels.sh` even selects its name (the samplers enumerate `.logos`
# files), so the anchored `-R` alternation quietly matches one fewer test.
#
# Measured when this gate landed: 6737 corpus `.logos`, 14 of them in no suite.
# The gate holds the set against a ledger in BOTH directions, so a new one is
# red and a fixed one is red too.
add_test(
    NAME logos_00_corpus_registration
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/corpus_registration_gate.sh
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/unregistered.ledger
)
set_tests_properties(logos_00_corpus_registration PROPERTIES
    TIMEOUT 120
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
)

# ── THE RECORDED LESSONS ABOUT LYING GATES, AS RULES ─────────────────────────
# `bug_exit_code_gate_lies` records eight ways a gate answers "nothing is wrong"
# when what happened is "I could not look". Each was diagnosed, fixed at the
# class and WRITTEN DOWN — and MEASURED 2026-08-01, two of them were live again
# in artifacts written the same week to stop gates from lying: 272 failure codes
# returned through an 8-bit exit status (code 256 read as SUCCESS), and two
# `awk … | grep -q` under `pipefail`, one of them in the negative form where a
# SIGPIPE turns a present forbidden string into an absent one.
#
# A written lesson that is not mechanized gets re-made. This runs the rules on
# every configure-and-test, and each rule proves it still bites by catching its
# own planted canary in the same invocation (exit 4 if one stops).
# ⚠ `--build-dir` IS NOT OPTIONAL, and that is the point. The set of shell gates
# is the set of scripts CTEST INVOKES, and only a configured build knows it. The
# previous form walked three directory names and reported clean over 23 of the
# 42 registered gates; the 24 it never opened held 15 live violations. Without
# this argument gate_lint.py exits 3 ("could not look") rather than falling back
# to a directory walk, which is the same defect wearing the fixed rule's name.
add_test(
    NAME logos_00_gate_lint
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/gate_lint.py --selftest
            --build-dir=${CMAKE_BINARY_DIR}
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(logos_00_gate_lint PROPERTIES
    TIMEOUT 300
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
)

# ── THE OPEN LAYOUT DECLINES — A LEDGER THAT CAN ONLY SHRINK ─────────────────
#
# A `pass` test named here compiles WITHOUT `LOGOS_VERIFY_LAYOUT=1`, because
# with it the compile is a hard error. Each entry states which type is declined
# and why the defect is not in the layout law, and each is held in BOTH
# directions by `logos_00_layout_decline_ledger`: a program that stops declining
# is as red as one that starts. A silent exclusion is what this whole arc is
# about; this is the exclusion written down, counted, and re-measured.
#
#   zone_zvec_two_zones — `layout_of` declines `zvec.HRel$G1$T`: the fully
#     concrete `ZVec$G2$i64$Writ` carries a field still typed `HRel<T>` with T
#     unbound, because the GAT `Z::Ptr<T>` resolved its constructor without
#     substituting its argument. That is a MONO substitution defect, not a
#     layout one — the law was handed a type that has no instance anywhere, and
#     answering "no layout" is correct. `{8,8}` is accidentally right here
#     (`HRel` is one `i64`), which is why nothing had ever noticed; it is not
#     right in general, so the decline stays until mono substitutes the GAT.
set(LAYOUT_DECLINE_LEDGER zone_zvec_two_zones)

# ── THE OPEN mlir-gen SELF-DIAGNOSES — A LEDGER THAT CAN ONLY SHRINK ─────────
#
# R2: a message mlir-gen emits ABOUT ITSELF ("statement DROPPED", "unknown
# tagged enum") must make the compile FAIL. Before that rule mlir-gen had NO
# diagnostic channel: it printed to stderr, dropped the statement, wrote the
# object file and returned 0 — so `run_test.sh`'s pass-mode check ("logosc
# succeeded") was blind to a vanished WRITE, and a test passed while its
# program was being miscompiled.
#
# MEASURED when the rule landed: 7 corpus programs tripped it. Every one was
# ALREADY miscompiled and passing. THREE have since been FIXED and their rows
# deleted (empty-struct-braces-b151, core_1_adv_unit_field,
# irrefutable-unit-b142 — see mlir_gen_bug.ledger for each cause); the
# remaining FOUR are a mono instantiation that never happened (a type-arg that
# stayed `<error>` / an unsubstituted fn-type param) and a generic
# struct-pattern binding. Each is excluded with its ground written in
# tests/logos/mlir_gen_bug.ledger, and held in BOTH directions by
# `logos_00_mlir_gen_bug_ledger`: a program that stops tripping is as red as
# one that starts.
#
# THE EXCLUDED SET LIVES IN THE LEDGER AND NOWHERE ELSE. It used to be repeated
# here as a CMake list, which made the excuse a property of who remembered to
# add a name rather than of the artifact — and the copies drifted: `pat_6`
# stayed red on the full suite while its ledger row was green, because only one
# of the two registration loops consulted the list. Every test is now handed
# the ledger PATH unconditionally and logosc asks whether the program it is
# compiling is named in it.

# ── THE SEPARATOR-CLASS CLAIMS AN EXIT CODE CANNOT SEE ──────────────────────
# Three separator fixtures run CORRECTLY on a compiler that emits the wrong
# symbol (definition and call agree on the same wrong spelling), so the pass
# harness is blind to them by construction. This gate asserts the emitted TEXT,
# and pairs every claim with the exact spelling the defect produced.
add_test(
    NAME logos_00_sep_symbol_shape
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/sep_symbol_shape_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass
)
set_tests_properties(logos_00_sep_symbol_shape PROPERTIES
    TIMEOUT 120
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# ── R2's VERDICT MUST REACH THE EXIT CODE, AND NO fail TEST CAN SEE THAT ────
# `run_test.sh`'s fail mode runs logosc under `|| true` and greps stderr, so it
# passes identically on rc 0 and rc 1: the property "a self-diagnosed
# malfunction fails the compile" is unexpressible in the corpus. MEASURED at
# 0fb56500, `a = s as f64;` (s: str) printed "mlir_gen: unsupported cast",
# exited 0 and wrote a 1288-byte object with the assignment silently gone —
# because `coerce_to` returned a bare nullptr and whether the compile failed was
# decided by WHO CONSUMED IT (a `return` reports, an assignment did not). This
# gate asserts the rc, the absent object file and that the report went through
# the R2 sink; it carries its own instrument canary (a control program with a
# SUPPORTED cast must compile and leave an object), so a dead harness reports
# itself broken instead of reporting the compiler clean. It is a separate file
# rather than a run_test.sh mode so that pinning this costs the rest of the fail
# corpus nothing.
add_test(
    NAME logos_00_mlir_gen_exit_code
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/mlir_gen_exit_code_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/fail
)
set_tests_properties(logos_00_mlir_gen_exit_code PROPERTIES
    TIMEOUT 120
    LABELS  "logos;fail;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

add_test(
    NAME logos_00_mlir_gen_bug_ledger
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/mlir_gen_bug_ledger_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/mlir_gen_bug.ledger
            ${CMAKE_SOURCE_DIR}
)
set_tests_properties(logos_00_mlir_gen_bug_ledger PROPERTIES
    TIMEOUT 300
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# ── `type_apply` — THE ONLY name→Type BRIDGE, AND IT ABORTS ─────────────────
# A metaprog handler holds its target's name as a runtime `str` read off the
# AST. `type_apply` takes a name only as a source literal and calls
# `std::abort()` in mono on anything else — no file, no line, a core dump. That
# is the whole wall between a handler and a capability question about the type
# it was handed, and it is what prices the value-domain derivation, so it is
# MEASURED rather than argued.
#
# Neither row can be a corpus test. `run_test.sh` fail-mode asserts "non-zero
# and stderr contains X", which cannot tell a SIGABRT from a clean refusal — and
# that difference IS the finding. Pass-mode compares the binary's exit code,
# which turns "the compiler approved a program whose instance is a trap" into
# "the program returned 132". The exit codes are the measurement, so the gate
# reads them directly; it carries three canaries (reader, matcher, comparator)
# because a gate whose instrument has died reports a clean ledger exactly like a
# clean one.
add_test(
    NAME logos_00_type_apply_trap_ledger
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/type_apply_trap_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/type_apply_trap.ledger
            ${CMAKE_CURRENT_SOURCE_DIR}/type_apply_trap
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_00_type_apply_trap_ledger PROPERTIES
    TIMEOUT 300
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# ── THE SEPARATOR CLASS CANNOT REGROW SILENTLY ──────────────────────────────
# The fix carried the parts and put the surviving splits behind mangled_name.hpp,
# but nothing stopped the NEXT `find("__")` from being written tomorrow — and a
# lesson that is not mechanized gets re-learned. This lint counts raw separator
# splits per file (comments stripped, so documenting the class is not an offence)
# and holds the census against a ledger in BOTH directions. It carries two
# canaries of its own: a planted split must be seen, and a split inside a comment
# must not be — a lint that matches nothing certifies nothing.
add_test(
    NAME logos_00_separator_split_lint
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/separator_split_lint.sh
            ${CMAKE_SOURCE_DIR}/src/compiler
            ${CMAKE_CURRENT_SOURCE_DIR}/separator_split.ledger
)
set_tests_properties(logos_00_separator_split_lint PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lint;suite_lint;tier_commit"
)

# ── THE ROOT COORDINATE HAS ONE DEFINITION, AND ONE MEMBERSHIP ─────────────
# WG_ROOT_PARENT/KEY/IDX/ORD collapsed four hand-written spellings of the
# virtual-root coordinate into one definition. That closed the VALUE axis. It
# left the MEMBERSHIP open: nothing stopped a fifth producer, or a second
# reader, from open-coding `0i64, "", -1i64` again, and the failure is silent —
# a producer and the reader disagreeing by one literal makes `from g .field ...`
# compile and answer ZERO ROWS, with nothing to compare against.
# The site set was a four-bullet PROSE COMMENT, and prose does not fail a build.
# Ledgered in both directions, with two self-canaries. Predicate narrowed to the
# COORDINATE (the adjacent triple), not to `-1i64`, which is also the not-found
# sentinel: a lint that cries wolf gets disabled, which is the defect one level up.
add_test(
    NAME logos_00_root_coord_lint
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/root_coord_lint.sh ${CMAKE_SOURCE_DIR}
)
set_tests_properties(logos_00_root_coord_lint PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lint;suite_lint;tier_commit"
)

# ── THE SHARED-&T UB RULING CANNOT DRIFT BACK INTO PROSE ────────────────────
# `noalias`+`readonly` on a shared `&T` is licensed by a LANGUAGE rule
# (type.shared-ref.write-through-derived-ub), not by the optimizer. The emission
# is pinned by IR snapshots; the argument was not, and the last time it was only
# prose the bullet describing it drifted into a wrong answer that read like a
# considered caveat and survived every green gate for months.
#
# This lint holds five mechanical facts instead of any wording: the readonly
# write happens exactly once and is CONTAINED IN a type_is_freeze-implying block
# (by brace matching, so the hoisted-bool refactor is not a false alarm); the
# rule id joins the emission site to the rule corpus; the interior-mutability
# ROOT population is derived over ALL of stdlib and held both ways, together with
# the transitive layer and the axis joint into the IR snapshot; the const->mut
# laundering census matches the ledger in both directions; and every delimited
# `@claim … @endclaim` block is hashed against shared_ref_ub_claims.ledger.
#
# ⚠ THE THREE DEFECTS THAT MOTIVATED THE REWRITE WERE EACH REPRODUCED FIRST:
# pasting the false DEFERRED bullet back over the settled rule exited 0 (the id
# string was untouched); hoisting the predicate into a named bool exited 1 though
# it is semantics-identical; and stripping UnsafeCell out of Mutex/RwLock exited 0
# because the census read one hardcoded file. Twelve self-canaries now run every
# fact through the same readers on planted input — including one that must NOT
# fire on a commented-out emission, because mlir_gen_fn.cpp quotes the old wrong
# wording verbatim and the lint has to survive its own documentation.
add_test(
    NAME logos_00_shared_ref_ub_lint
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/shared_ref_ub_lint.sh
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/shared_ref_ub.ledger
            ${CMAKE_CURRENT_SOURCE_DIR}/shared_ref_ub_claims.ledger
)
set_tests_properties(logos_00_shared_ref_ub_lint PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lint;suite_lint;tier_commit"
)

# ── THE FREEZE LATTICE'S AXIS POPULATION IS DERIVED FROM THE PREDICATE ───────
# `type_is_freeze` makes SEVENTEEN separate verdicts and its IR gate pinned FIVE
# axes. MEASURED, not supposed: replace the element recursion in the TUPLE arm
# with `return true` and `&(Cell<i64>, i64)` moves to `noalias readonly` — a real
# noalias on an interior-mutable pointee — with all five axes green and L2 at
# 1881/1881. Writing a longer list of axes would have been the same defect again,
# so this gate derives both populations from the artifacts: the Kind enumerators
# from sema.hpp (50 = 40 named + 10 defaulted, held as arithmetic) and the verdict
# sites from the predicate's own body, each of which must name an `; AXIS` marker
# in the lattice .check or be recorded NO-AXIS with a stated reason. It carries
# five self-canaries through the same readers.
add_test(
    NAME logos_00_freeze_arm_coverage
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/freeze_arm_coverage.py
            ${CMAKE_SOURCE_DIR}
            --ledger ${CMAKE_CURRENT_SOURCE_DIR}/freeze_arms.ledger
            --check  ${CMAKE_CURRENT_SOURCE_DIR}/ir/param_attrs_freeze_lattice.check
)
set_tests_properties(logos_00_freeze_arm_coverage PROPERTIES
    TIMEOUT 60
    LABELS  "logos;lint;suite_lint;tier_commit"
)

add_test(
    NAME logos_00_layout_decline_ledger
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/layout_decline_ledger_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/layout_decline.ledger
            ${CMAKE_CURRENT_SOURCE_DIR}/pass
)
set_tests_properties(logos_00_layout_decline_ledger PROPERTIES
    TIMEOUT 300
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# ── ABI CLOSURE ───────────────────────────────────────────────────────────────
# The recorded ABI surface must be CLOSED under reachability: a type named by a
# recorded field list or enum payload must itself have a record. It was not, and
# the hole was not theoretical — `QEnv` was recorded carrying
# `f_ptrs:[fn(&[RtVal]) -> RtVal; 8]` while `RtVal`, a `pub enum` and the whole
# UDF registration surface, had none, so an added arm produced a BYTE-IDENTICAL
# spec and `--abi-diff` said ABI-PRESERVING (re-measured on this tree before the
# fix; after it, the same probe reports ABI-BREAKING).
#
# ⚠ `logos_00_`, NOT `logos_09_`. MEASURED: the L1/L2 gates tier is
# `-R '^logos_00_|^logos_07_ir_snapshot_'`, so the whole `logos_09_*` family —
# including `logos_09_rtval_domain`, written for this very arc — is L4-only and
# has never run on the commit that would break it. A gate off the per-commit
# path is the "a test MISSING from the suite" shape one level up.
#
# ⚠ NAMED `_abi_reachability`, NOT `_abi_closure`. MEASURED: the L1/L2 corpus
# sampler selects with `-R '_(<basename>|…)$'` built from the .logos corpus, and
# `closure` IS a corpus basename — so `logos_00_abi_closure` was captured by the
# corpus chunk as well as the gates tier, ran twice, and pushed the L2 tally from
# 1883 to 1884 even though that tally documents itself as EXCLUDING the gates. A
# gate whose name ends in `_<corpus basename>` is silently double-counted. One
# such collision already exists at HEAD (`logos_07_ir_snapshot_enum_match` vs the
# corpus `enum_match`); this arc does not add a second. The general fix — assert
# the gate set and the corpus selection are disjoint — is a change to what every
# commit costs and is not this slice's to take.
#
# Needs the BUILT stdlib: the check reads the `.abi-closure` sidecars emit_module
# writes next to the archives, so it depends on logosc AND on the archives being
# current (same dependency `scripts/abi-check.sh` has).
add_test(
    NAME logos_00_abi_reachability
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/abi_closure_gate.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
            ${CMAKE_SOURCE_DIR}/abi/logos.abi-closure-exempt
)
set_tests_properties(logos_00_abi_reachability PROPERTIES
    TIMEOUT 120
    LABELS  "logos;lint;suite_lint;tier_commit"
)

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

# ⚠ THE FIFTH WAY A GATE LIES: A WHOLE SUITE THAT VANISHES AND READS AS GREEN.
# This block used to search `NAMES FileCheck FileCheck-19 FileCheck-18` under
# `PATHS /usr/lib/llvm-19/bin /usr/lib/llvm-18/bin` and, finding nothing, print
# `message(STATUS "skipping IR snapshot suite")`. On any box off those two series
# ELEVEN tests silently stopped existing — and nothing pins the total, since
# ctest-summary.sh requires only that a pass/fail SUMMARY LINE exist, never an
# absolute count. A suite short eleven tests reported green. Worse, on THIS box
# the block was live but WRONG-VERSIONED: it resolved llvm-19's FileCheck while
# the compiler was built against LLVM 20.
#
# TWO CHANGES, and the second is the one that matters:
#   1. DERIVE THE TOOL FROM THE TOOLCHAIN ACTUALLY SELECTED. LOGOS_LLVM_MAJOR is
#      already computed at the top level (either from the stock-series pick or
#      from the Debian-layout LOGOS_LLVM_DIR), so the search follows the compiler
#      instead of a hand-maintained pair of series numbers. Adding a name to a
#      list is what this repo keeps learning not to do.
#   2. ABSENCE IS RED. If FileCheck cannot be found, the suite does not shrink —
#      it registers logos_07_ir_snapshot_UNAVAILABLE, which fails. A box that
#      cannot run eleven tests must SAY it could not run them.
#      ⚠ THIS IS A DELIBERATE BEHAVIOUR CHANGE for a box that legitimately has no
#      FileCheck (a bare packaging box): it used to get a silent skip and now gets
#      a failing test. Set LOGOS_ALLOW_NO_FILECHECK=ON to opt out — that turns the
#      absence into a recorded, deliberate decision instead of a quiet one, which
#      is the whole distinction being drawn here.
#
# A NEW cache variable name is used on purpose: FILECHECK_BIN is already cached
# as an llvm-19 path in every existing build tree, and find_program does not
# re-search a cached hit — the stale entry would outlive the fix.
unset(FILECHECK_BIN CACHE)   # evict the old, possibly wrong-series, cached hit
find_program(LOGOS_FILECHECK_BIN
    NAMES FileCheck-${LOGOS_LLVM_MAJOR} FileCheck
    HINTS ${LOGOS_LLVM_DIR}/../../../bin
          /usr/lib/llvm-${LOGOS_LLVM_MAJOR}/bin
    DOC "LLVM FileCheck for IR snapshot tests (must match LOGOS_LLVM_MAJOR)")
set(FILECHECK_BIN "${LOGOS_FILECHECK_BIN}")

# The version is CHECKED, not assumed: a FileCheck picked up off PATH from
# another series is exactly the state this box was in.
if (FILECHECK_BIN)
    execute_process(COMMAND ${FILECHECK_BIN} --version
                    OUTPUT_VARIABLE _fc_ver ERROR_QUIET
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT _fc_ver MATCHES "version ${LOGOS_LLVM_MAJOR}\\.")
        message(WARNING
            "logos tests: ${FILECHECK_BIN} does not report LLVM ${LOGOS_LLVM_MAJOR} "
            "(got: ${_fc_ver}). The IR snapshot suite will run under a different "
            "series than the compiler was built against; treat an IR failure as a "
            "toolchain suspect before a codegen suspect.")
    endif()
endif()

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
            LABELS  "logos;ir_snapshot;suite_ir;tier_commit"
            ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
        )
    endforeach()
elseif (LOGOS_ALLOW_NO_FILECHECK)
    message(WARNING
        "logos tests: FileCheck not found and LOGOS_ALLOW_NO_FILECHECK=ON — the IR "
        "snapshot suite is DELIBERATELY absent from this build. The suite total is "
        "smaller than a full box's by the number of tests/logos/ir/*.check files; "
        "do not read a green run here as covering codegen.")
else()
    # The suite does not shrink; it says it could not run. `cmake -E false` exits
    # 1 unconditionally, so this is a plain red test with an explanation attached.
    file(GLOB _ir_missing CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ir/*.check)
    list(LENGTH _ir_missing _ir_missing_n)
    add_test(
        NAME logos_07_ir_snapshot_UNAVAILABLE
        COMMAND ${CMAKE_COMMAND} -E env
                LOGOS_IR_TESTS_MISSING=${_ir_missing_n}
                ${CMAKE_COMMAND} -E false
    )
    set_tests_properties(logos_07_ir_snapshot_UNAVAILABLE PROPERTIES
        TIMEOUT 30
        LABELS  "logos;ir_snapshot;suite_ir;tier_commit"
    )
    message(WARNING
        "logos tests: no FileCheck for LLVM ${LOGOS_LLVM_MAJOR} — ${_ir_missing_n} IR "
        "snapshot tests CANNOT RUN. Registering logos_07_ir_snapshot_UNAVAILABLE as a "
        "failing test rather than shrinking the suite: a gate that disappears reads as "
        "a pass, and nothing else pins the suite total. Install FileCheck, or pass "
        "-DLOGOS_ALLOW_NO_FILECHECK=ON to record the absence as deliberate.")
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
    # The spec corpus is registered HERE, not through logos_add_grouped_test,
    # so the R2 ledger exclusion has to be applied here too — a ledger that
    # covers only the tests one registration path happens to reach is the same
    # blindness it exists to remove. (MEASURED: pat_6 stayed red on the full
    # suite while its ledger row was green, because this loop did not set it.)
    set(_spec_env "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}")
    list(APPEND _spec_env "LOGOS_MLIRGEN_BUG_LEDGER=${CMAKE_CURRENT_SOURCE_DIR}/mlir_gen_bug.ledger")
    add_test(NAME ${_test_name}
        COMMAND ${TEST_RUNNER} pass $<TARGET_FILE:logosc> ${LOGOS_FILE} ${EXP_FILE})
    set_tests_properties(${_test_name} PROPERTIES
        TIMEOUT 30 LABELS "logos;corpus;pass;suite_spec"
        ENVIRONMENT "${_spec_env}")
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
        TIMEOUT 15 LABELS "logos;corpus;fail;suite_spec"
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
    LABELS  "logos;lint;suite_lint;tier_full"
)

# --gen-dir round-trip gate: quote-emitted modules dump as .gen.logos, the
# dump reparses cleanly (no fidelity fallback), and DWARF references it —
# the debugger lists generated code. One representative per corpus axis:
# vis/trait/impl/struct (vis_antiquote) and dyn/opt uses (opt_use).
#
# ⚠ The corpus was all HAND-WRITTEN quotes, which is why three renderer holes
# survived in it: hand-written test quotes happen not to contain a unit enum
# variant, a `?`, or a bare block at statement position. Deem's emitters write
# all three on every query, so the last three cases are EMITTER output —
# `wql_tuple_first_e2e` returns `Option::None` (ENUM_LIT), the mapping binder in
# `query_mapping_runtime_e2e` writes `f(x)?` (TRY_EXPR), and
# `quote_item_exprblob_cursor` splices a bare `{ … }` scope (EXPR_STMT). Each
# rendered to text that PARSED and meant something else, which the round-trip's
# top-level item census cannot see.
#
# ⚠ A FOURTH of that shape: a PACKAGE-QUALIFIED call. `pkg.path::fn(args)` keeps
# its qualifier in RECEIVER + QUAL_PARTS, and the renderer read CALLEE alone, so
# the dump called by bare name — parses, censuses the same, resolves elsewhere.
# `quote_item_qual_call_render` is the case, and it is a BEHAVIOUR test, not a
# text grep: the trigger module declares same-named fns with incompatible
# signatures, and a chunk's own package wins over a merely-loaded one, so a lost
# qualifier is a type error HERE under `-g`. It was found the same way as the
# other three — trama!'s emitter writes `logos.std.wql.el::wql_upper(…)` on
# every `{{ upper(x) }}`, and nothing hand-written in the corpus did.
foreach(GENDIR_CASE quote_item_vis_antiquote quote_item_opt_use quote_item_parse_as quote_item_wstatic_typearg quote_item_wstatic_const quote_item_ctrfamily_impl quote_item_dyn_typearg wql_tuple_first_e2e query_mapping_runtime_e2e quote_item_exprblob_cursor quote_item_qual_call_render)
    add_test(
        NAME logos_09_gendir_${GENDIR_CASE}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_gendir_test.sh
                $<TARGET_FILE:logosc>
                ${CMAKE_CURRENT_SOURCE_DIR}/pass/${GENDIR_CASE}.logos
    )
    set_tests_properties(logos_09_gendir_${GENDIR_CASE} PROPERTIES
        TIMEOUT 60
        LABELS  "logos;pass;suite_semantic_core;tier_full"
        ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
    )
endforeach()

# Import-set gate: a synth module's USES must be a SET. Three independent
# sources feed it (the quote's own imports, the handler module's imports baked
# into the blob, the user module's imports merged after) and none sees the
# others, so the intersection used to land twice and sema_collect warned once
# per duplicate per emitted item — on code the user cannot edit. Deem emits one
# quote per generated fn, so the noise scales with the query count (819 warnings
# per full build before the dedup sweep, 6 after — and those 6 are real
# duplicates in hand-written source).
#
# ⚠ The --gen-dir corpus cannot gate this: the dump renderer prints each package
# once, so the dumps look identical either way. Only stderr shows it.
add_test(
    NAME logos_09_no_dup_use_in_synth
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/no_dup_use_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_first_fold_e2e.logos
)
set_tests_properties(logos_09_no_dup_use_in_synth PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# Emitted-body SHAPE gate (ADR 0024 S5): a Deem-emitted fn's body is FLAT.
# `#(body)` fills the fn's own body slot (`fn_body`), so nothing survives of the
# earlier route where a `parse_block` fragment spliced at a STATEMENT position
# and wrapped every generated body in one extra scope.
#
# ⚠ That regression is SILENT in every other gate: an extra scope compiles,
# runs, reparses, and keeps its DWARF, so build/L4/--gen-dir round-trip are all
# unchanged and only a human reading generated code ever pays. Until this gate
# the property was held by an author eyeballing dumps once per conversion.
# One fixture per emitter family: the three thin collapses + find (first_fold),
# the loop nest (join), the group-by nest (aggregate), the multi-item private
# rel/SCC helpers (stratified), and a signature naming a factory handle
# (ctr_family_streams).
foreach(FLATBODY_CASE wql_first_fold_e2e wql_join_e2e wql_aggregate_e2e wql_datalog_stratified_e2e deem_ctr_family_streams)
    add_test(
        NAME logos_09_flat_emitted_body_${FLATBODY_CASE}
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/flat_body_gate.sh
                $<TARGET_FILE:logosc>
                ${CMAKE_CURRENT_SOURCE_DIR}/pass/${FLATBODY_CASE}.logos
    )
    set_tests_properties(logos_09_flat_emitted_body_${FLATBODY_CASE} PROPERTIES
        TIMEOUT 60
        LABELS  "logos;pass;suite_semantic_core;tier_full"
        ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
    )
endforeach()

# Plan SIZE gate (ADR 0024 S4): a source's size is a fact the plan ASKS FOR, and
# both answers are recorded — the number's provenance when the source declares a
# reporter, the absence when it does not.
#
# ⚠ Nothing consumes the number yet, by design: this phase supplies the fact. So
# the fixture would keep passing with the whole size plane deleted, and the only
# thing that can hold the property is the plan's own account of what it asked —
# stderr under LOGOS_TRACE_PLAN=1, which appears in no artifact.
add_test(
    NAME logos_09_plan_size_asked
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/plan_size_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/deem_source_size.logos
)
set_tests_properties(logos_09_plan_size_asked PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# JOIN-ORDER gate (ADR 0024 S4): the emitted fn carries BOTH join orders and one
# run-time discriminant, the plan reports the decision AND both refusals, and the
# sort that makes the two orders interchangeable is total.
#
# ⚠ The fixture asserts the ROWS, which is what says the reorder is sound — but a
# compiler that quietly emitted ONE nest returns the same rows and passes it. Only
# the artifact and the decision channel can say a second order exists at all.
add_test(
    NAME logos_09_join_order_either_side
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/join_order_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_join_order_dyn_e2e.logos
)
set_tests_properties(logos_09_join_order_either_side PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# PREPARED-PLAN gate (ADR 0024 S4i): the plan is a runtime VALUE — a type, a
# `prepare` that decides once, a `run` that applies it, and the direct fn as
# prepare-then-run.
#
# ⚠ Three properties the fixture cannot see: that the surface exists for queries
# with NO run-time choice too (uniformity is what makes it usable), that `prepare`
# contains no loop (a prepare that ran the query would make re-checking a plan cost
# as much as the query), and that the per-row body is emitted ONCE.
add_test(
    NAME logos_09_prepared_plan_surface
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/prepared_plan_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_prepared_plan_e2e.logos
)
set_tests_properties(logos_09_prepared_plan_surface PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# DEFERRED-PLAN gate (ADR 0024 S4j): the plan's SECOND decision point — inside
# `run`, past the prelude's unavoidable materializations and before the join's
# index, where a rel's `len()` is a field read the nest was going to make anyway.
#
# ⚠ Four properties the fixture cannot see, and each is the difference between a
# deferral worth having and one that costs more than it saves: that the two reads
# and the branch stay at BODY LEVEL (a decision paid per row is not a plan), that
# the number read is the MATERIALIZED length rather than the input parameter the
# planner could have measured earlier (the two agree on the mirror data set, so
# behaviour only separates them on one side), that a deferred `prepare` measures
# NOTHING (which is what keeps `agrees` affordable for every plan), and that the two
# decision points are tellable apart in the artifact and in the trace.
add_test(
    NAME logos_09_deferred_plan_second_point
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/deferred_plan_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_deferred_plan_e2e.logos
)
set_tests_properties(logos_09_deferred_plan_second_point PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# INCREMENTAL-ELIGIBILITY gate (P1): the set of queries that gain an incremental
# handle is DERIVED from the plan and asked for by nobody, so it is a derivation
# that no caller and no fixture can enumerate from the outside. The companion
# fixture pins the POSITIVE direction by calling the derived fns; the negative
# direction has nothing to name, so the gate reads the decision AND ITS GROUND off
# `LOGOS_TRACE_PLAN`, asserts the population by COUNT as well as by membership, and
# proves it can still speak by running its own matcher over a trace broken five
# ways — the fifth being the rel refusal flipped to EMITTED, which is the exact
# regression that removing `incr_eligible`'s rel clause produces and the one that
# let a handle over a declared `rel` answer wrong while tracing success.
add_test(
    NAME logos_09_incr_eligibility_population
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/incr_eligibility_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_incr_eligibility_matrix.logos
)
set_tests_properties(logos_09_incr_eligibility_population PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# INCREMENTAL RETRACTION gate — the same shape, one axis in. Eligibility asks
# whether a query gets a HANDLE; retractability asks whether that handle's delta
# may carry a NEGATIVE weight, and the answer moves the EMITTED SIGNATURE
# (`<q>_apply` + `<q>_retract`, or P1's insert-only `<q>_epoch` alone). The
# negative direction has nothing a fixture can name, and its ONE remaining
# ground — `(S+x)-x != S` over an f64 accumulator — is an OPEN decision rather
# than a missing feature: if the refusal ever silently lapsed, the corpus would
# go on passing and a decision nobody took would have been taken.
#
# ⚠ `tier_commit`: THIS GATE MUST BE REACHABLE PER COMMIT. What it guards is a
# derivation nothing else in the corpus can enumerate, and its subject is an open
# decision, so a gate that only runs in a full-suite tier is a gate the commit
# that breaks it never meets. The label is how it declares that, rather than
# being hand-added to a selector regex that lives somewhere else.
add_test(
    NAME logos_09_incr_retraction_population
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/incr_retraction_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_incr_retract_matrix.logos
)
set_tests_properties(logos_09_incr_retraction_population PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# SCC DRIVER COUNT gate (P3c): one cross-epoch semi-naive driver per admitted
# recursive SCC, not one per admitted MEMBER.
#
# ⚠ `tier_commit`, AND FOR A REASON THE CONTROL MEASURED RATHER THAN A HABIT.
# `emit_rel_fns` emitted the `_i` driver from inside the member loop, which was
# correct only because every multi-member SCC was refused. Widening the admission
# made it emit the SAME item once per member — and MEASURED 2026-08-07 by
# restoring the old loop and rebuilding the whole stdlib, EVERY test in the tree
# stayed green: the compiler takes the duplicate definition silently. A defect no
# answer can see is exactly the kind that returns, so the emitter reports what it
# emitted and this counts it. It runs in ~2 compiles, so there is no cost ground
# for putting it out of reach of the commit that would break it.
add_test(
    NAME logos_09_incr_scc_driver_count
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/incr_scc_driver_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_incr_rel_mutrec_epochs.logos
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_incr_rel_rec_epochs.logos
)
set_tests_properties(logos_09_incr_scc_driver_count PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# MUTREC REFUSAL gate: the handle is EMITTED and the RETRACTION, specifically, is
# the thing withheld — asserted in ONE compile so the two cannot drift apart.
#
# ⚠ THIS EXISTS BECAUSE THE THREE `fail/wql_incr_mutrec_*` DOORS WERE MEASURED
# TO PASS UNDER A STRICTLY STRONGER REFUSAL. Restore P3b's `nmem != 1` — the
# mutually recursive SCC gets no incremental surface at all — rebuild the stdlib,
# and all three doors stay GREEN while only `pass/wql_incr_rel_mutrec_epochs.logos`
# reds. `run_test.sh` fail mode asks that stderr CONTAIN a name-not-found, and the
# name is missing under every refusal that reaches the query. A green door is
# therefore not evidence about the retraction clause; this gate is the eligible
# control the `fail/` files cannot carry, because a file that must not compile has
# no room for a positive assertion.
#
# `tier_commit`: one compile, and the commit that would break it is the same
# commit that touches these two clauses.
add_test(
    NAME logos_09_incr_mutrec_refusal
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/incr_mutrec_refusal_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_incr_rel_mutrec_epochs.logos
)
set_tests_properties(logos_09_incr_mutrec_refusal PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# MULTI-ORDER JOIN gate (ADR 0024 S4k): the join-order axis is a decision over a
# DERIVED SET, not a transposition of the first pair. What the fixture cannot see is
# everything about the orders that did NOT run: which permutations were considered,
# which constraint each loser failed, and that the plan declined WHOLE rather than
# searched a set chosen by enumeration order when the artifact bound was reached.
#
# ⚠ Also gated: the cost model has exactly ONE copy (generated code calls
# `logos.std.wql.join_cost`, it does not spell the arithmetic — N queries spelling
# the same rule is N chances to drift from the account the trace gives), and the
# discriminant is read outside every loop, checked by BRACE NESTING rather than by
# column: a four-way branch chain legitimately puts its later tests at deeper
# indents while still standing above every loop, so an indent assertion would have
# to be loosened and would then stop meaning anything.
add_test(
    NAME logos_09_join_order_beyond_pair
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/join_order_multi_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_join_order_multi_e2e.logos
)
set_tests_properties(logos_09_join_order_beyond_pair PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# KEY-ORDER LICENCE gate (ADR 0024 S4k, C1): carrying more than one nest is licensed
# by the `order by` sort determining the sequence, which is a claim about the key's
# TYPE and was asserted unconditionally, then narrowed to "is the CLASS totally
# ordered" — the wrong axis. An f64 key is PartialOrd, so the tiebreak never engages
# and two nests returned two sequences; a u64 key's emitted comparison is TOTAL and
# is the signed reinterpretation, so it passed the totality test while ranking 2^63
# below 0. The question is whether the EMITTED comparison realizes the order the
# query NAMED. The fixture asserts the rows; this asserts the DECISION: each refusal
# names the failed condition and its remedy, the artifact carries one nest, the
# ground travels into the plan's own `why` (so `explain()` can state it), and the
# i64/str/u32-keyed queries over the same sources keep all four orders — `str` is
# faithfully ordered and `u32` widens losslessly, so the narrowing is about FIDELITY
# and not about non-integer or unsigned keys.
add_test(
    NAME logos_09_join_order_key_fidelity
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/join_order_key_fidelity_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_join_order_key_fidelity_e2e.logos
)
set_tests_properties(logos_09_join_order_key_fidelity PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# HASHABLE-KEY AGREEMENT (ADR 0024 S6 — the value domain, tier 1 vs tier 2).
#
# "May this type be a hash key" is decided twice, by two different KINDS of
# artifact: the rel column asks the LANGUAGE (`impl Hash`, sema_collect.cpp
# `check_rel_column_types`), and the EL lattice answers from a hand-written
# 21-row table (`el_index_key_ok`). MEASURED 2026-08-04: the two populations
# agreed member for member — 12 admitted, 9 refused. RE-MEASURED 2026-08-05,
# after the 128-bit and packed widths gained their `Hash`/`Eq`/`Ord` instances
# and the lattice's width heuristic was replaced by its INT clause: 18 admitted,
# 3 refused (f32 f64 String), agreeing again. That agreement is a measurement,
# and either side can still move alone and silently — deleting `impl Hash for
# i56` flips tier 1 while the lattice keeps admitting it.
#
# The gate's population comes out of a RUN of `el_ty_at(0..el_ty_arity())`, so a
# row added to the lattice is covered without editing the gate, and it proves its
# own comparator on a planted flip before it reports anything.
add_test(
    NAME logos_09_el_hashable_agreement
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/el_hashable_agreement_gate.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_09_el_hashable_agreement PROPERTIES
    TIMEOUT 180
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# WIDE-KEY INDEX TIER (ADR 0024 S6 — the value domain, the decision side).
#
# The agreement gate above pins that the two "may this type be a hash key"
# artifacts answer the same SET. This one pins what that answer BUYS: the six
# widened types (u128 i128 u56 i56 u24 i24) reach the join cascade's HASH tier,
# keyed in their own type and not in the class representative — which for a
# 128-bit key would drop the high bits. Read off `LOGOS_TRACE_PLAN`, over the same
# fixture whose rows `pass/wql_join_wide_key_e2e.logos` asserts; that fixture
# passed before the widening too, because a loop join returns the same pairs. A `String`
# step on the same run must still report `loop join`, so a trace that admitted
# everything cannot satisfy this.
add_test(
    NAME logos_09_wide_key_index_tier
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/wide_key_index_tier_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_join_wide_key_e2e.logos
)
set_tests_properties(logos_09_wide_key_index_tier PROPERTIES
    TIMEOUT 120
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# SHADOWED-COLUMN gate (ADR 0024 S3/S6 + S4k C1): a column's type is a fact about a
# BINDING (row var, column), not about the spelling of a field. The flat name
# dictionary let two joined sources declaring the same column name overwrite each
# other, last write wins. The fixture asserts the rows; this asserts the two things
# it cannot see — the EMITTED element type per query as TEXT (both queries returned
# `Vec<u64>` before the fix), and the PLANNER's order-axis verdict, which is the
# serious half: an f64 key re-typed i64 by the other source's `k` was GRANTED the
# reorder licence C1 exists to refuse, with no diagnostic anywhere.
#
# ⚠ The i64-keyed CONTROL over the SAME two sources is half the gate: a planner that
# had simply stopped reordering would pass a refusal-only assertion.
add_test(
    NAME logos_09_wql_shadowed_column
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/wql_shadowed_column_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/wql_shadowed_column_e2e.logos
)
set_tests_properties(logos_09_wql_shadowed_column PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# COLUMN-DECLARATION LAYER gate (the value-domain arc, D2). `run_test.sh`
# asserts CONTAINMENT and has no way to assert that a string is ABSENT — so the
# four column-declaration fail fixtures cannot themselves tell "refused by the
# front end, naming the column the author wrote" from "refused eventually, by
# the MLIR verifier or inside a generated blob", which is what all four did
# before the refusal existed. Both of those are red; only one of them is a
# diagnostic. This asserts the LAYER, and reads the mlir-gen dump file from the
# filesystem so a reworded message cannot silence it.
add_test(
    NAME logos_09_wql_column_decl_layer
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/wql_column_decl_layer_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/fail
)
set_tests_properties(logos_09_wql_column_decl_layer PROPERTIES
    TIMEOUT 120
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# Renderer CONTENT gate: the gendir gate above only proves the dump reparses,
# and the trait-object / type-argument degradations all reparse into something
# ELSE (`impl Fam<dyn Tag>` → `impl Fam<Tag>`). Assert the rendered text.
add_test(
    NAME logos_09_render_type_fidelity
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/render_type_fidelity.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass/quote_item_dyn_typearg.logos
)
set_tests_properties(logos_09_render_type_fidelity PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# WHY-SIZE gate (ADR 0024 S4q): the rendered justification reaches the artifact only
# through `explain()`, so a program that never asks for one links none of the prose.
#
# ⚠ THIS IS A GATE ON A MEASURED CLAIM, and it exists because the previous one was
# wrong by two orders of magnitude and nothing could see it: `f5688fb8` asserted
# "+8 bytes" from total FILE size at -O2, while the loaded image at -O0 (what the
# suite compiles) had grown +1168 on `wql_deferred_plan_e2e`. No fixture can weigh
# its own binary, so the property lives here or nowhere.
add_test(
    NAME logos_09_why_size_reachability
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/why_size_gate.sh
            $<TARGET_FILE:logosc>
)
set_tests_properties(logos_09_why_size_reachability PROPERTIES
    TIMEOUT 180
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# Diagnostics: --diag-format=json + structured exit codes (B0.2).
add_test(
    NAME logos_09_diag_json_format
    COMMAND ${CMAKE_SOURCE_DIR}/tests/diag/json_format.sh
            $<TARGET_FILE:logosc>
)
set_tests_properties(logos_09_diag_json_format PROPERTIES
    TIMEOUT 30
    LABELS  "logos;diag;suite_diag;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
    LABELS  "logos;lforge;suite_lforge;tier_full"
)

# Archive integrity: a package must never vanish from a module archive
# silently. Covers (1) size is not a publish limit — a ~6000-line single
# source still publishes and links, (2) `ar r` never truncating no longer
# leaves stale members from a previous build, (3) emit_module reads the
# finished .a back through the loader's own reader and fails loudly.
add_test(
    NAME logos_15_archive_integrity
    COMMAND ${CMAKE_SOURCE_DIR}/tests/lforge/archive_integrity.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_15_archive_integrity PROPERTIES
    TIMEOUT 300
    LABELS  "logos;lforge;suite_lforge;tier_full"
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
    LABELS  "logos;lforge;suite_lforge;tier_full"
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
    LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
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
        LABELS  "logos;lforge;suite_lforge;tier_full"
    )
endif()

# ── PLAN INDEPENDENCE, CORPUS-WIDE (ADR 0024 S4r) ────────────────────────────
# The property every multi-candidate query has: the answer does not depend on
# WHICH carried nest the cost function picked. Until this test it was asserted by
# three fixtures that write `order_ix` by hand; the other nine that carry more
# than one order were exercised only on the nest their data happened to select.
#
# This runs THE EXISTING FIXTURES, unmodified, once per carried order, against
# their own committed `.expected` — 12 fixtures, 35 forced runs — with the
# emitter's discriminant pinned by `LOGOS_WQL_FORCE_ORDER`. It is not a fixture
# of its own and adds no `.logos` file: a metamorphic gate's whole point is that
# it needs no new oracle.
#
# ⚠ Its cost is a corpus of compiles (127 census + 35 forced), so it is a plain
# `add_test` with no sampled label: the L1-L3 samplers enumerate `.logos` files
# and cannot reach it, and L4 runs it with everything else.
add_test(
    NAME logos_09_plan_independence
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/plan_independence_gate.sh
            $<TARGET_FILE:logosc>
            ${CMAKE_CURRENT_SOURCE_DIR}/pass
            ${CMAKE_CURRENT_SOURCE_DIR}/run_test.sh
            12
)
set_tests_properties(logos_09_plan_independence PROPERTIES
    TIMEOUT    900
    PROCESSORS 12
    LABELS     "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR};LOGOS_TEST_LIB_DIR=${CMAKE_CURRENT_BINARY_DIR}"
)

# Layout-engine agreement (see layout_engine_agreement_gate.sh for the full
# argument). FIVE engines answer "how many bytes, at what alignment, with which
# field at which offset" — `layout_of` over TypeRef, `mlir_abi_size` over the
# emitted LLVM-dialect type, `llvm::DataLayout` on the mirrored llvm::Type
# (the layout the object is emitted with), `sema_abi_layout` and
# `mono_abi_layout`. The last two run and are CONSUMED before mlir-gen exists,
# so they record every answer into a ledger and `verify_layout_engines()` checks
# the record. A disagreement is a compile error, so the corpus carries the claim.
#
# ⚠ What the corpus does NOT carry is that the check LOOKED at anything, or that
# it looked at every ENGINE. This gate generates a lattice over (scalar type ×
# struct shape) AND over the composition shapes whose branches were missing from
# an engine (union, tagged/C-like/niche enum, zero-sized payload, generic
# instantiation, nestings), asserts the verifier saw at least as many new types
# as the lattice defines, and floors the ledger count PER ENGINE so one engine
# cannot go silent behind another's number. It then RUNS a generated oracle that
# measures where a DST's tail really lands and where a field really is, and runs
# the authored fixtures.
add_test(
    NAME logos_09_layout_engine_agreement
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/layout_engine_agreement_gate.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
)
set_tests_properties(logos_09_layout_engine_agreement PROPERTIES
    TIMEOUT 300
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# The compile-UNIT partition and the ORDER it derives (unit_graph.cpp). Before
# this test the suite could not see either: `ctest -N` reported the same total
# with and without unit_graph.cpp, so a green run said exactly nothing about
# the artifact — and a MEASURED false claim of independence on the canonical
# case-1 shape lived behind that green. See unit_graph_gate.sh for the seven
# assertions and what each one caught, including its own negative control
# (LOGOS_UNITS_NO_ORDER_FACTS=1 must turn every positive result off).
#
# Its fixtures (unit_order / unit_cycle / unit_xmod_consumer / unit_chain) are
# module builds, so it is a plain add_test with no sampled label — the L1-L3
# samplers enumerate `.logos` files under pass/fail and cannot reach it.
add_test(
    NAME logos_09_unit_graph
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/unit_graph_gate.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/unit_graph_gate
)
set_tests_properties(logos_09_unit_graph PROPERTIES
    TIMEOUT 900
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# THE DYNAMIC/INCREMENTAL VALUE DOMAIN (`RtVal`), AND THE LEDGER OF ITS `_`
# ARMS — WHICH IS EMPTY, AND THAT IS THE DELIVERABLE.
#
# MEASURED 2026-08-04 by a control build that added one arm (`F32(f32)`) to the
# real enum: the widening broke exactly THREE matches (rt_kind / rt_truthy /
# RBinds__set_val) and nothing else, because the other 24 match sites ended in
# `_` and absorbed the new shape as Null / 0 / 0.0 / "" — for a value type, a
# wrong answer rather than a compile error. A three-site tripwire in front of a
# 27-site surface.
#
# MEASURED 2026-08-05, after all 24 `_` arms were replaced by or-patterns naming
# the variants they answer for (same bodies, same variant sets — no behaviour
# moved): the identical control build names 27 sites. That number, and not the
# gate alone, is what the slice delivered.
#
# The gate therefore no longer pins a hand-written list of exhaustive matches.
# It derives the exhaustive set as (all match sites) minus (the ledger), and
# holds `rtval_fallback.ledger` in BOTH directions: a `_` with no ledger line is
# red, a ledger line matching no `_` is red, and an entry must name the
# mechanism, a RUNNING gate that exercises it, and what would delete it. Eight
# canaries run through the same code paths in the same invocation — the ledger's
# expected content is EMPTY, so "read it, nothing to report" and "never opened
# it" would otherwise print the same line.
#
# TWO THINGS IT STILL CANNOT MAKE LOUD, both PRINTED on every green run rather
# than left implied:
#   • `rt_eq` / `rt_cmp` dispatch on the i32 CODE `rt_kind` returns, not on the
#     enum — 27 call sites that no exhaustiveness check reaches. After a
#     widening, `rt_eq` answers `false` for two EQUAL values of the new shape
#     (every row its own group in group-by / distinct / join) and `rt_cmp` sorts
#     them all at 0.0. A live defect, reported with its measurement, not fixed
#     here: closing it means deciding what comparing a Node to a Null MEANS.
#   • `logosc --emit-abi` produced a BYTE-IDENTICAL spec and `--abi-diff`
#     answered "ADDED: 0 record(s) / VERDICT: ABI-PRESERVING", because
#     `is_deem_api_type` (emit_module.cpp) keeps `RtVal` out of the spec even
#     though the public UDF signature is `fn(&[RtVal]) -> RtVal`. So
#     `scripts/abi-check.sh` is blind here BY CONSTRUCTION.
#
# Static analysis over the sources, the ledger and the committed spec — no build
# needed, which is why it carries no logosc dependency and no library
# environment.
add_test(
    NAME logos_09_rtval_domain
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/rtval_domain_gate.sh
            ${CMAKE_SOURCE_DIR}/stdlib/mem/deem
            ${CMAKE_SOURCE_DIR}/abi/logos.abi
            ${CMAKE_CURRENT_SOURCE_DIR}/rtval_fallback.ledger
)
set_tests_properties(logos_09_rtval_domain PROPERTIES
    TIMEOUT 60
    LABELS  "logos;pass;suite_semantic_core;tier_commit"
)

# SHARDED EMISSION (LOGOS_EMIT_SHARDS / LOGOS_EMIT_WORKERS). MEASURED 2026-08-04:
# repo-wide, three occurrences of `LOGOS_EMIT_SHARDS`, all three in
# src/compiler/emit_module.cpp (two comments and the getenv). Nothing under
# tests/, scripts/, tools/ or any CMakeLists set it, so every shard sweep in that
# arc was run BY HAND and a regression on that path was invisible at every tier —
# a behaviour-changing knob no test enters is indistinguishable from a passing
# one, which is the same defect class as a test missing from the suite. The
# source comment at the shards default asks for exactly this ("Pin it before
# anyone leans on this path").
#
# The gate compares two EMITTED ARCHIVES of one fixture module (one object vs
# four) symbol by symbol, plus the W=1 sequential control against W=4. It does
# NOT stand on the consumer running: MEASURED, with the shard predicate forced to
# skip every body, all four shard objects were valid and defined ZERO globals,
# and the consumer still linked and still printed the right answer, because the
# archive's `.wr0` carries the AST and logosc recompiles what it cannot find.
# See emit_shards_gate.sh for each assertion and the deliberate break it caught.
#
# ⚠ Cost 20.1 s measured through ctest on a quiet box (37.9 s with three sibling
# builds running), and it is a plain add_test with no sampled label: its
# fixture is a module build, and the L1-L3 samplers enumerate `.logos` files
# under pass/ and fail/ and cannot reach it. L4 runs it with everything else,
# where it is off the critical path. Deliberately NOT wired into test-levels.sh
# L2 — that is +40% on a 65 s tier for a knob that is off by default and, by the
# 2026-08-04 price measurement (sharding buys build time and sells run time about
# one for one), stays off for anything shipped or measured.
add_test(
    NAME logos_09_emit_shards
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/emit_shards_gate.sh
            $<TARGET_FILE:logosc>
            ${LOGOS_BUILD_LIB_DIR}
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR}/emit_shards_gate
)
set_tests_properties(logos_09_emit_shards PROPERTIES
    TIMEOUT 600
    LABELS  "logos;pass;suite_semantic_core;tier_full"
    ENVIRONMENT "LOGOS_LIB_DIR=${LOGOS_BUILD_LIB_DIR}"
)

# ── THE ENUMERATOR (tests/exhaustive) ────────────────────────────────────────
#
# `tests/logos/{pass,fail}` holds AUTHORED fixtures: a person chose the case and
# wrote the expectation. That is why six defects — including four SILENT WRONG
# ANSWERS — survived six rounds of green gates: coverage was measured against
# what the corpus contained, not against what the type lattice and the query
# shapes admit. `tests/exhaustive` is a GENERATOR over those axes, and every
# expectation is computed by `model.py` (Python exact integers, IEEE `struct`),
# which shares no code, algorithm or assumption with the compiler.
#
# TWO TIERS, and both are stated rather than sampled:
#
#   logos_26_exhaustive_smoke — 86 programs / 11 316 cases, ~34 s.
#       cast, cmp, arith, pattern and poison IN FULL, plus a DIAGONAL through
#       deem: one program per payload type, walking the nine (field-position ×
#       caller-context) combinations, so all 20 types and all 3 positions and
#       all 3 caller contexts appear. Runs at EVERY level of test-levels.sh —
#       it is wired in explicitly there, because it is not a corpus member and
#       the L1/L2/L3 samplers enumerate the corpus.
#
#   logos_26_exhaustive_full — 246 programs / 13 508 cases, ~153 s.
#       The whole product, including the 160 deem programs the smoke tier does
#       not run (the rest of the per-type field-position × caller-context
#       product). L4 only — it is a ctest test with no corpus name, so the
#       samplers never select it and `ctest` runs it when it runs everything.
#
# EACH TIER ASSERTS TWO THINGS.
#   (1) ZERO wrong answers, absolutely, with no ledger and no exception. A
#       wrong answer at any tier fails the gate.
#   (2) The set of COMPILE REFUSALS is EXACTLY `refusals.ledger`, checked in
#       both directions: an unlisted refusal is a new defect, and a listed
#       refusal that now compiles means the arc landed and the ledger is stale.
# and each checks the CORPUS DIGEST first, so "the generator produces the same
# corpus today" is a gate rather than a hope.
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/exhaustive/harness.py)
    foreach(EXH_TIER smoke full)
        # ── WHY THE SMOKE TIER IS `tier_explicit` AND NOT `tier_commit` ──────
        # It DOES run on every commit — but not through a tier selector. It has
        # its own block in test-levels.sh with its own accounting and its own
        # reported line, because it is 12 684 generated cases behind one ctest
        # name and folding it into the gates count would misreport both. Under a
        # two-value vocabulary neither label could be true: `tier_commit` would
        # run it a SECOND time per level (~34 s wasted, failure double-counted),
        # and `tier_full` would state the opposite of when it runs. So the
        # vocabulary carries a third value meaning "selected by name from a
        # bespoke harness block", and test-levels.sh selects that block on the
        # label — so this is a declaration, not a decoration.
        if(EXH_TIER STREQUAL smoke)
            set(_exh_timeout 600)
            set(_exh_tier tier_explicit)
        else()
            set(_exh_timeout 1800)
            set(_exh_tier tier_full)
        endif()
        add_test(
            NAME logos_26_exhaustive_${EXH_TIER}
            COMMAND ${CMAKE_SOURCE_DIR}/tests/exhaustive/run_tier.sh ${EXH_TIER}
                    $<TARGET_FILE:logosc>
                    ${LOGOS_BUILD_LIB_DIR}
        )
        set_tests_properties(logos_26_exhaustive_${EXH_TIER} PROPERTIES
            TIMEOUT    ${_exh_timeout}
            PROCESSORS 12
            LABELS     "logos;exhaustive;suite_exhaustive;${_exh_tier}"
        )
    endforeach()
endif()
