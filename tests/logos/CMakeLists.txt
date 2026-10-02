# Copyright 2026 Victor Smirnov
# Logos compiler test fixtures — requires logosc to be built.

if (NOT TARGET logosc)
    message(STATUS "logos tests: logosc not built — skipping")
    return()
endif()

# ── THIS FILE BUILDS THE TEST FIXTURES; IT REGISTERS NO TESTS ────────────────
# Test registration moved to tests/lt_registry.py: scripts/lt discovers the
# tests from the tree itself (scripts/lt_discover.py), so ctest has nothing to
# run here. What stays is everything the tests CONSUME that the build produces:
# the stdlib layers and the fixture archives the corpus links with `-l` (their
# paths are mirrored by `ARCHIVES` in tests/lt_registry.py — rename one here and
# rename it there).

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

# ── THE MEMORIA LIBRARY, as three in-tree fixture archives ──────────────────
#
# `conuco/memoria` is a separate incubating project that builds with lforge, and
# its SOURCES stay there. Its TESTS live here, in the corpus, with every other
# test — they were the one population nothing in the repository ran, and being a
# separate suite is exactly what let them rot through a whole borrow-checker arc
# (47e34581). A test that needs a library is an ordinary thing; a test suite that
# needs its own runner is not.
#
# The manifests are ours (tests/logos/memoria_lib/*.module) rather than lforge's
# generated ones because `root` must be repo-root-relative here, and because we
# name the `depends` explicitly — lforge's generated manifests carry none, which
# makes logosc RECOMPILE every dependency body instead of linking it.
set(MEMORIA_SRC ${CMAKE_SOURCE_DIR}/conuco/memoria/src)
set(MEMORIA_MOD ${CMAKE_CURRENT_SOURCE_DIR}/memoria_lib)
set(MEMORIA_CTR_BIN     ${CMAKE_CURRENT_BINARY_DIR}/libmemoria-ctr.a)
set(MEMORIA_STORE_BIN   ${CMAKE_CURRENT_BINARY_DIR}/libmemoria-store.a)
set(MEMORIA_TESTKIT_BIN ${CMAKE_CURRENT_BINARY_DIR}/libmemoria-testkit.a)
file(GLOB MEMORIA_CTR_SOURCES     ${MEMORIA_SRC}/ctr/*.logos)
file(GLOB MEMORIA_STORE_SOURCES   ${MEMORIA_SRC}/store/*.logos)
file(GLOB MEMORIA_TESTKIT_SOURCES ${MEMORIA_SRC}/testkit/*.logos)
add_custom_command(
    OUTPUT  ${MEMORIA_CTR_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${MEMORIA_MOD}/memoria-ctr.module -L ${STDLIB_BIN_DIR}
            -o ${MEMORIA_CTR_BIN}
    DEPENDS logosc ${MEMORIA_MOD}/memoria-ctr.module ${MEMORIA_CTR_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libmemoria-ctr.a"
)
# `depends memoria-ctr` is resolved through -L, so the store archive needs the
# ctr archive's DIRECTORY on the search path as well as its build dependency.
add_custom_command(
    OUTPUT  ${MEMORIA_STORE_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${MEMORIA_MOD}/memoria-store.module -L ${STDLIB_BIN_DIR}
            -L ${CMAKE_CURRENT_BINARY_DIR}
            -o ${MEMORIA_STORE_BIN}
    DEPENDS logosc ${MEMORIA_MOD}/memoria-store.module ${MEMORIA_STORE_SOURCES}
            ${MEMORIA_CTR_BIN}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libmemoria-store.a"
)
add_custom_command(
    OUTPUT  ${MEMORIA_TESTKIT_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${MEMORIA_MOD}/memoria-testkit.module -L ${STDLIB_BIN_DIR}
            -o ${MEMORIA_TESTKIT_BIN}
    DEPENDS logosc ${MEMORIA_MOD}/memoria-testkit.module ${MEMORIA_TESTKIT_SOURCES}
            ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_LCM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libmemoria-testkit.a"
)
add_custom_target(memoria_binaries ALL
    DEPENDS ${MEMORIA_CTR_BIN} ${MEMORIA_STORE_BIN} ${MEMORIA_TESTKIT_BIN})

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

# trait_ident_chain: the HOMONYM-ACROSS-ARCHIVES chain, same recipe as
# three_layer above, two tiers instead of three — the third tier is the
# consumer fixture tests/logos/pass/trait_ident_pkg_chain.logos itself.
#   liblhom.a  declares its own `trait Hash` + `impl Hash for f64`
#   libhmid.a  manifest `depends lhom`, and DELIBERATELY NO `prelude` line,
#              so the consumer never acquires lhom's names transitively.
# The consumer links BOTH archives (lhom's facts must reach mono; only its
# IMPORT VIEW must stay clean) and asks `has_trait::<f64, Hash>()` about the
# stdlib `Hash` that owns the bare registry slot.
set(TRAIT_IDENT_CHAIN_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/trait_ident_chain)
set(TRAIT_IDENT_CHAIN_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TRAIT_IDENT_CHAIN_LHOM_MANIFEST ${TRAIT_IDENT_CHAIN_DIR}/lhom.module)
set(TRAIT_IDENT_CHAIN_HMID_MANIFEST ${TRAIT_IDENT_CHAIN_DIR}/hmid.module)
set(TRAIT_IDENT_CHAIN_LHOM_BIN ${TRAIT_IDENT_CHAIN_OUT_DIR}/liblhom.a)
set(TRAIT_IDENT_CHAIN_HMID_BIN ${TRAIT_IDENT_CHAIN_OUT_DIR}/libhmid.a)
file(GLOB TRAIT_IDENT_CHAIN_LHOM_SOURCES ${TRAIT_IDENT_CHAIN_DIR}/lhom/*.logos)
file(GLOB TRAIT_IDENT_CHAIN_HMID_SOURCES ${TRAIT_IDENT_CHAIN_DIR}/hmid/*.logos)
add_custom_command(
    OUTPUT  ${TRAIT_IDENT_CHAIN_LHOM_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${TRAIT_IDENT_CHAIN_LHOM_MANIFEST}
            -o ${TRAIT_IDENT_CHAIN_LHOM_BIN}
    DEPENDS logosc ${TRAIT_IDENT_CHAIN_LHOM_MANIFEST} ${TRAIT_IDENT_CHAIN_LHOM_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: liblhom.a (trait_ident_chain)"
)
add_custom_command(
    OUTPUT  ${TRAIT_IDENT_CHAIN_HMID_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${TRAIT_IDENT_CHAIN_HMID_MANIFEST}
            -L ${TRAIT_IDENT_CHAIN_OUT_DIR}
            -o ${TRAIT_IDENT_CHAIN_HMID_BIN}
    DEPENDS logosc ${TRAIT_IDENT_CHAIN_HMID_MANIFEST} ${TRAIT_IDENT_CHAIN_HMID_SOURCES} ${TRAIT_IDENT_CHAIN_LHOM_BIN} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libhmid.a (trait_ident_chain; depends lhom)"
)
add_custom_target(trait_ident_chain_binary ALL DEPENDS
    ${TRAIT_IDENT_CHAIN_LHOM_BIN} ${TRAIT_IDENT_CHAIN_HMID_BIN})

# trait_blanket_chain: the BLANKET-alias twin of trait_ident_chain, ONE tier.
#   libbmid.a  declares `trait Error` — a homonym of `logos.lang.error::Error`,
#              which owns the bare registry slot because the stdlib is collected
#              first — plus a BLANKET impl of it and a BOUNDED generic template
#              `impl<T: Error> Holder<T>`.
# The consumer (tests/logos/pass/trait_blanket_bare_alias_bound.logos) links this
# one archive, so `Holder<i64>::store` reaches mono only through the method
# worklist and its bound is checked against the BARE spelling "Error" — the only
# program in the corpus where `Mono::populate_trait_engine_`'s blanket-alias
# branch both fires AND decides an outcome. See the fixture header for both arms.
set(TRAIT_BLANKET_CHAIN_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/trait_blanket_chain)
set(TRAIT_BLANKET_CHAIN_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(TRAIT_BLANKET_CHAIN_BMID_MANIFEST ${TRAIT_BLANKET_CHAIN_DIR}/bmid.module)
set(TRAIT_BLANKET_CHAIN_BMID_BIN ${TRAIT_BLANKET_CHAIN_OUT_DIR}/libbmid.a)
file(GLOB TRAIT_BLANKET_CHAIN_BMID_SOURCES ${TRAIT_BLANKET_CHAIN_DIR}/bmid/*.logos)
add_custom_command(
    OUTPUT  ${TRAIT_BLANKET_CHAIN_BMID_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${TRAIT_BLANKET_CHAIN_BMID_MANIFEST}
            -o ${TRAIT_BLANKET_CHAIN_BMID_BIN}
    DEPENDS logosc ${TRAIT_BLANKET_CHAIN_BMID_MANIFEST} ${TRAIT_BLANKET_CHAIN_BMID_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libbmid.a (trait_blanket_chain)"
)
#   libbprobe.a  the MIRROR of bmid: it declares NO homonym and exports
#              `bare_probe<T: Error>` bounded by the STDLIB `logos.lang.error::Error`,
#              so the bound's identity IS the bare registry slot. Two consumers
#              link it, and both declare their own `trait Error` + blanket:
#              fail/trait_blanket_homonym_bound_refused (the local blanket must
#              NOT answer the stdlib bound — it compiled rc 0 before the fix)
#              and pass/trait_blanket_homonym_bound_admits (a real stdlib-Error
#              implementor must still be admitted with the homonym in scope).
#              The pair is what separates "homonym separated out" from "every
#              Error bound refused"; the fail door's message alone cannot.
#              `Ok1`/`make_ok1` live in the archive because a file declaring its
#              own `Error` cannot spell the stdlib one (no qualified-impl form).
set(TRAIT_BLANKET_CHAIN_BPROBE_MANIFEST ${TRAIT_BLANKET_CHAIN_DIR}/bprobe.module)
set(TRAIT_BLANKET_CHAIN_BPROBE_BIN ${TRAIT_BLANKET_CHAIN_OUT_DIR}/libbprobe.a)
file(GLOB TRAIT_BLANKET_CHAIN_BPROBE_SOURCES ${TRAIT_BLANKET_CHAIN_DIR}/bprobe/*.logos)
add_custom_command(
    OUTPUT  ${TRAIT_BLANKET_CHAIN_BPROBE_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${TRAIT_BLANKET_CHAIN_BPROBE_MANIFEST}
            -o ${TRAIT_BLANKET_CHAIN_BPROBE_BIN}
    DEPENDS logosc ${TRAIT_BLANKET_CHAIN_BPROBE_MANIFEST} ${TRAIT_BLANKET_CHAIN_BPROBE_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libbprobe.a (trait_blanket_chain)"
)
add_custom_target(trait_blanket_chain_binary ALL DEPENDS
    ${TRAIT_BLANKET_CHAIN_BMID_BIN} ${TRAIT_BLANKET_CHAIN_BPROBE_BIN})

# ── D1 round 5 / H6: a borrow FLOW that lives in an ARCHIVED callee ──────────
#   libbcxa.a  exports `pub fn stash2(c: &C, v: &mut Vec<B>)` — the canonical
#              door-F shape, where the `B` is manufactured INSIDE the callee
#              and stored through its `&mut` out-param, so nothing at the call
#              site is borrow-carrying and only the per-function borrow-FLOW
#              summary can decide it. Here that body is behind an archive
#              boundary, which is what the fixture pair is about.
#              pass/bc_d1r5_h6_cross_archive_admits links it and reads BEFORE
#              mutating; it must compile and run. The mutate-after twin is
#              deliberately NOT registered — re-measured on this tree it is
#              ADMITTED (the archived body is an unmaterialized ExternalRef, so
#              the summary reports UNAVAILABLE), and an `.expected` asserting a
#              clean compile of unsound code records the defect as intended.
#              The refusing edge is fail/bc_d1r5_h6_local_body_twin, the same
#              program with the callee's body in the consumer's own file; the
#              two together state that WHERE A BODY LIVES should be inert and
#              say openly that today it is not.
set(BCXA_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/bc_cross_archive)
set(BCXA_MANIFEST ${BCXA_DIR}/bcxa.module)
set(BCXA_BIN      ${CMAKE_CURRENT_BINARY_DIR}/libbcxa.a)
file(GLOB BCXA_SOURCES ${BCXA_DIR}/bcxa/*.logos)
add_custom_command(
    OUTPUT  ${BCXA_BIN}
    COMMAND $<TARGET_FILE:logosc>
            --emit-module ${BCXA_MANIFEST}
            -o ${BCXA_BIN}
    DEPENDS logosc ${BCXA_MANIFEST} ${BCXA_SOURCES} ${LOGOS_LANG_BIN} ${LOGOS_MEM_BIN} ${LOGOS_STD_BIN}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Building test fixture: libbcxa.a (bc_cross_archive)"
)
add_custom_target(bc_cross_archive_binary ALL DEPENDS ${BCXA_BIN})
