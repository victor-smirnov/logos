// Logos project — https://github.com/victor-smirnov/logos
//
// ArenaPool exerciser (Phase 0 of multi-arena IR refactor).
// Tests: register / lookup / unregister / refcount semantics / arena_id
// non-reuse / dep validation.

#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/verification/assert.hpp>

#include <cstdio>

using namespace logos::hermes;

// Allocate a dummy MemHolder for testing. Caller owns one ref.
static MemHolder* make_dummy_holder() {
    logos::InitTag tag;
    auto* h = new MemHolder(tag, 4096, ArenaMode::GrowableSingleChunk);
    LOGOS_ASSERT(tag.ok(), "POOL-TEST-INIT-001",
        "MemHolder construction must succeed in test setup");
    h->ref();  // caller-side ref
    return h;
}

static void test_register_lookup_drop() {
    std::printf("--- register / lookup / unregister ---\n");

    InMemoryArenaPool pool;

    auto* h1 = make_dummy_holder();
    auto* h2 = make_dummy_holder();

    auto handle1 = pool.register_module(h1, "coremeta", {});
    auto handle2 = pool.register_module(h2, "alloc", {"coremeta"});

    LOGOS_ASSERT(handle1.arena_id.is_valid(), "POOL-TEST-REG-001",
        "handle1.arena_id must be valid (got {})", handle1.arena_id.value);
    LOGOS_ASSERT(handle2.arena_id.is_valid(), "POOL-TEST-REG-002",
        "handle2.arena_id must be valid (got {})", handle2.arena_id.value);
    LOGOS_ASSERT(handle1.arena_id != handle2.arena_id, "POOL-TEST-REG-003",
        "distinct registrations must get distinct arena_ids");
    LOGOS_ASSERT(handle1.name == "coremeta", "POOL-TEST-REG-004",
        "handle1.name preserved");
    LOGOS_ASSERT(handle2.depends_on.size() == 1, "POOL-TEST-REG-005",
        "alloc must have exactly 1 dep, got {}", handle2.depends_on.size());
    LOGOS_ASSERT(handle2.depends_on[0] == handle1.arena_id, "POOL-TEST-REG-006",
        "alloc's resolved dep must equal coremeta's arena_id");

    // get() returns the same MemHolder pointer we registered.
    LOGOS_ASSERT(pool.get(handle1.arena_id) == h1, "POOL-TEST-GET-001",
        "get(coremeta_aid) returns h1");
    LOGOS_ASSERT(pool.get(handle2.arena_id) == h2, "POOL-TEST-GET-002",
        "get(alloc_aid) returns h2");
    LOGOS_ASSERT(pool.get(INVALID_ARENA_ID) == nullptr, "POOL-TEST-GET-003",
        "get(INVALID_ARENA_ID) returns nullptr");
    LOGOS_ASSERT(pool.get(arena_id_t{9999}) == nullptr, "POOL-TEST-GET-004",
        "get(out-of-range) returns nullptr");

    // find_by_name round-trips.
    auto found = pool.find_by_name("alloc");
    LOGOS_ASSERT(found.has_value(), "POOL-TEST-FIND-001",
        "find_by_name('alloc') should hit");
    LOGOS_ASSERT(found->arena_id == handle2.arena_id, "POOL-TEST-FIND-002",
        "find_by_name returns correct arena_id");
    LOGOS_ASSERT(found->depends_on == handle2.depends_on, "POOL-TEST-FIND-003",
        "find_by_name returns correct deps");

    auto missing = pool.find_by_name("stdlib");
    LOGOS_ASSERT(!missing.has_value(), "POOL-TEST-FIND-004",
        "find_by_name on unregistered name returns nullopt");

    // Refcount semantics: each holder has caller's ref + pool's ref = 2.
    LOGOS_ASSERT(h1->use_count() == 2, "POOL-TEST-REFCNT-001",
        "h1 refcount = 2 (caller + pool), got {}", h1->use_count());
    LOGOS_ASSERT(h2->use_count() == 2, "POOL-TEST-REFCNT-002",
        "h2 refcount = 2 (caller + pool), got {}", h2->use_count());

    // unregister drops the pool's ref.
    pool.unregister(handle2.arena_id);
    LOGOS_ASSERT(h2->use_count() == 1, "POOL-TEST-UNREG-001",
        "h2 refcount = 1 after pool drop, got {}", h2->use_count());
    LOGOS_ASSERT(pool.get(handle2.arena_id) == nullptr, "POOL-TEST-UNREG-002",
        "get on unregistered arena_id returns nullptr");
    LOGOS_ASSERT(!pool.find_by_name("alloc").has_value(), "POOL-TEST-UNREG-003",
        "find_by_name on unregistered name returns nullopt");

    // Other entries unaffected.
    LOGOS_ASSERT(pool.get(handle1.arena_id) == h1, "POOL-TEST-UNREG-004",
        "h1 still accessible after h2 unregister");
    LOGOS_ASSERT(h1->use_count() == 2, "POOL-TEST-UNREG-005",
        "h1 refcount unchanged by h2 unregister");

    // Caller-side cleanup.
    h1->unref();  // pool still holds ref, h1 alive
    h2->unref();  // refcount → 0, MemHolder deleted via private dtor

    // h1 still alive via pool's ref — verify pool's ref kept it.
    LOGOS_ASSERT(pool.get(handle1.arena_id) == h1, "POOL-TEST-LIFETIME-001",
        "pool's ref keeps h1 alive after caller drop");

    // Pool destruction drops the last ref on h1.
    std::printf("  PASS\n");
}

static void test_arena_id_not_reused() {
    std::printf("--- arena_id append-only (no reuse after unregister) ---\n");

    InMemoryArenaPool pool;
    auto* h1 = make_dummy_holder();
    auto* h2 = make_dummy_holder();

    auto a = pool.register_module(h1, "m1", {});
    auto first_id = a.arena_id;
    pool.unregister(a.arena_id);

    auto b = pool.register_module(h2, "m2", {});
    LOGOS_ASSERT(b.arena_id != first_id, "POOL-TEST-NOREUSE-001",
        "arena_id must not be reused after unregister (invariant #2). "
        "first={}, second={}", first_id.value, b.arena_id.value);
    LOGOS_ASSERT(b.arena_id.value == first_id.value + 1, "POOL-TEST-NOREUSE-002",
        "arena_ids assigned sequentially; expected {}, got {}",
        first_id.value + 1, b.arena_id.value);

    h1->unref();
    pool.unregister(b.arena_id);
    h2->unref();
    std::printf("  PASS\n");
}

static void test_name_reuse_after_unregister() {
    std::printf("--- name can be re-registered after unregister ---\n");

    InMemoryArenaPool pool;
    auto* h1 = make_dummy_holder();
    auto* h2 = make_dummy_holder();

    auto a = pool.register_module(h1, "same_name", {});
    pool.unregister(a.arena_id);

    // Name is now free; reuse should succeed (different arena_id though).
    auto b = pool.register_module(h2, "same_name", {});
    LOGOS_ASSERT(b.arena_id != a.arena_id, "POOL-TEST-NAMEREUSE-001",
        "name re-registration gets fresh arena_id");

    auto found = pool.find_by_name("same_name");
    LOGOS_ASSERT(found.has_value() && found->arena_id == b.arena_id,
        "POOL-TEST-NAMEREUSE-002",
        "find_by_name after re-registration returns the new arena_id");

    h1->unref();
    pool.unregister(b.arena_id);
    h2->unref();
    std::printf("  PASS\n");
}

static void test_global_pool_singleton() {
    std::printf("--- global_arena_pool() singleton ---\n");

    auto& p1 = global_arena_pool();
    auto& p2 = global_arena_pool();
    LOGOS_ASSERT(&p1 == &p2, "POOL-TEST-GLOBAL-001",
        "global_arena_pool() returns the same instance");
    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Phase 1.A: ExternalRef + LirArenaRoot type infrastructure tests
// ---------------------------------------------------------------------------

static void test_external_ref_layout() {
    std::printf("--- ExternalRef layout invariants ---\n");

    // Compile-time invariants (also asserted via static_assert in the header).
    LOGOS_ASSERT(sizeof(ExternalRef) == 7, "EXTREF-LAYOUT-001",
        "sizeof(ExternalRef) must be 7 (got {})", sizeof(ExternalRef));
    LOGOS_ASSERT(alignof(ExternalRef) == 1, "EXTREF-LAYOUT-002",
        "alignof(ExternalRef) must be 1 (got {})", alignof(ExternalRef));

    // Round-trip accessor semantics.
    auto ref = ExternalRef::make(arena_id_t{0x123456}, 0xDEADBEEF);
    LOGOS_ASSERT(ref.arena_id() == arena_id_t{0x123456}, "EXTREF-ACCESS-001",
        "arena_id roundtrip: got {}", ref.arena_id().value);
    LOGOS_ASSERT(ref.obj_id() == 0xDEADBEEF, "EXTREF-ACCESS-002",
        "obj_id roundtrip: got 0x{:x}", ref.obj_id());

    // High-byte boundary on arena_id (24-bit limit).
    auto ref_max = ExternalRef::make(arena_id_t{0xFFFFFF}, 0x12345678);
    LOGOS_ASSERT(ref_max.arena_id() == arena_id_t{0xFFFFFF}, "EXTREF-ACCESS-003",
        "max arena_id roundtrip");
    LOGOS_ASSERT(ref_max.obj_id() == 0x12345678, "EXTREF-ACCESS-004",
        "max obj_id roundtrip");

    // Equality.
    LOGOS_ASSERT(ref != ref_max, "EXTREF-EQ-001", "distinct ExternalRefs not equal");
    auto ref_dup = ExternalRef::make(arena_id_t{0x123456}, 0xDEADBEEF);
    LOGOS_ASSERT(ref == ref_dup, "EXTREF-EQ-002", "duplicate ExternalRefs equal");

    std::printf("  PASS\n");
}

static void test_external_ref_arena_roundtrip() {
    std::printf("--- ExternalRef stored in arena via arena_put ---\n");

    auto doc = make_doc(4096).get();
    auto& arena = HermesAccess::arena(doc);

    // Write 3 ExternalRefs into the arena.
    auto ref1 = ExternalRef::make(arena_id_t{1}, 100);
    auto ref2 = ExternalRef::make(arena_id_t{42}, 0x12345678);
    auto ref3 = ExternalRef::make(arena_id_t{0xFFFFFF}, 0xFFFFFFFF);

    auto p1_exp = arena_put<ExternalRef>(arena, ref1);
    LOGOS_ASSERT(p1_exp.has_value(), "EXTREF-ARENA-002", "arena_put ref1 must succeed");
    auto p2_exp = arena_put<ExternalRef>(arena, ref2);
    LOGOS_ASSERT(p2_exp.has_value(), "EXTREF-ARENA-003", "arena_put ref2 must succeed");
    auto p3_exp = arena_put<ExternalRef>(arena, ref3);
    LOGOS_ASSERT(p3_exp.has_value(), "EXTREF-ARENA-004", "arena_put ref3 must succeed");

    // Read back via arena_get.
    auto r1 = arena_get(*p1_exp);
    auto r2 = arena_get(*p2_exp);
    auto r3 = arena_get(*p3_exp);

    LOGOS_ASSERT(r1 == ref1, "EXTREF-ARENA-005", "ref1 roundtrip");
    LOGOS_ASSERT(r2 == ref2, "EXTREF-ARENA-006", "ref2 roundtrip");
    LOGOS_ASSERT(r3 == ref3, "EXTREF-ARENA-007", "ref3 roundtrip");

    // Verify TypeTag prefix is exactly 1 byte (single-byte range).
    // type_hash::ExternalRef = 110, which fits in [1, 222].
    auto tag = TypeTag::read_before(reinterpret_cast<uint8_t*>(*p1_exp));
    LOGOS_ASSERT(tag.type_code() == type_hash::ExternalRef, "EXTREF-ARENA-008",
        "TypeTag::type_code must be {} (got {})",
        type_hash::ExternalRef, tag.type_code());
    LOGOS_ASSERT(tag.byte_length() == 1, "EXTREF-ARENA-009",
        "TypeTag prefix must be 1 byte for ExternalRef (got {})", tag.byte_length());

    // Total footprint check: 1 (tag) + 7 (payload) = 8 bytes.
    // Verified indirectly via byte_length() + sizeof(ExternalRef) asserts.

    std::printf("  PASS\n");
}

static void test_lir_arena_root() {
    std::printf("--- LirArenaRoot construction + view ---\n");

    auto doc = make_doc(8192).get();
    auto& arena = HermesAccess::arena(doc);

    // Build the metadata structure:
    //   LirArenaRoot {
    //     SCHEMA_VERSION : 1
    //     MODULE_NAME    : "testmod"
    //     DEPS           : ["coremeta", "alloc"]
    //     DIRECTORY      : [null, ptr_to_dummy_obj]  (entry at obj_id 1)
    //   }

    // Allocate the module name string.
    auto name_str = doc.make_string("testmod").get();

    // Build the DEPS array (two strings).
    auto deps_arr = doc.make_array(2).get();
    {
        auto dep1 = doc.make_string("coremeta").get();
        auto dep2 = doc.make_string("alloc").get();
        deps_arr.push_back(dep1.to_anyval()).get();
        deps_arr.push_back(dep2.to_anyval()).get();
    }

    // Build the DIRECTORY array. Slot 0 = null sentinel (matches invariant
    // #13: obj_id 0 is INVALID). Slot 1 = pointer to a dummy ExternalRef
    // (we're testing the directory shape, not its semantic content yet).
    auto dir_arr = doc.make_array(4).get();
    {
        dir_arr.push_back(AnyVal{}).get();  // slot 0: null sentinel

        // Slot 1: pointer to a dummy ExternalRef in this arena.
        auto* dummy_p = arena_put<ExternalRef>(
            arena, ExternalRef::make(arena_id_t{7}, 99)).get();
        auto dummy_off = HermesAccess::offset_of(doc, dummy_p);
        dir_arr.push_back(AnyVal::from_offset(dummy_off)).get();
    }

    // Build the root TinyObjectMap and tag it as LirArenaRoot via schema_type_code.
    auto root_map = doc.make_tiny_map(4).get();
    root_map.ptr()->set_schema_type_code(type_hash::LirArenaRoot);

    root_map.put(lir_arena_root::SCHEMA_VERSION,
                  AnyVal::from_value<uint32_t>(
                      lir_arena_root::CURRENT_VERSION,
                      static_cast<uint8_t>(type_hash::U24))).get();
    root_map.put(lir_arena_root::MODULE_NAME, name_str.to_anyval()).get();
    root_map.put(lir_arena_root::DEPS,        deps_arr.to_anyval()).get();
    root_map.put(lir_arena_root::DIRECTORY,   dir_arr.to_anyval()).get();

    // Set DocumentHeader.root_offset via public set_root.
    doc.set_root(root_map);

    // Now read everything back via LirArenaRootView.
    LOGOS_ASSERT(root_map.ptr()->schema_type_code() == type_hash::LirArenaRoot,
        "LIRROOT-VIEW-001",
        "schema_type_code must be LirArenaRoot ({}); got {}",
        type_hash::LirArenaRoot, root_map.ptr()->schema_type_code());

    // Construct the LirArenaRootView from the TinyMapView slice of root_map.
    LirArenaRootView lar(static_cast<const TinyMapView&>(root_map));

    LOGOS_ASSERT(lar.schema_version() == lir_arena_root::CURRENT_VERSION,
        "LIRROOT-VIEW-002", "schema_version mismatch: {}", lar.schema_version());

    auto modname = lar.module_name();
    LOGOS_ASSERT(!modname.is_null(), "LIRROOT-VIEW-003", "module_name must be non-null");
    LOGOS_ASSERT(modname.view() == "testmod", "LIRROOT-VIEW-004",
        "module_name mismatch: '{}'", std::string(modname.view()));

    auto deps = lar.deps();
    LOGOS_ASSERT(!deps.is_null(), "LIRROOT-VIEW-005", "deps array must be non-null");
    LOGOS_ASSERT(deps.size() == 2, "LIRROOT-VIEW-006",
        "deps size mismatch: {}", deps.size());
    {
        StringView d0(deps.get(0).to_offset(), doc.holder());
        StringView d1(deps.get(1).to_offset(), doc.holder());
        LOGOS_ASSERT(d0.view() == "coremeta", "LIRROOT-VIEW-007",
            "dep[0] mismatch: '{}'", std::string(d0.view()));
        LOGOS_ASSERT(d1.view() == "alloc", "LIRROOT-VIEW-008",
            "dep[1] mismatch: '{}'", std::string(d1.view()));
    }

    auto dir = lar.directory();
    LOGOS_ASSERT(!dir.is_null(), "LIRROOT-VIEW-009", "directory must be non-null");
    LOGOS_ASSERT(dir.size() == 2, "LIRROOT-VIEW-010",
        "directory size mismatch: {}", dir.size());
    LOGOS_ASSERT(dir.get(0).is_null(), "LIRROOT-VIEW-011",
        "directory slot 0 must be null sentinel");
    LOGOS_ASSERT(dir.get(1).is_pointer(), "LIRROOT-VIEW-012",
        "directory slot 1 must be a pointer");

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Phase 1.B: binary_codec + publish helpers + register_lir_arena
// ---------------------------------------------------------------------------

static void test_external_ref_binary_codec_roundtrip() {
    std::printf("--- ExternalRef binary_codec roundtrip ---\n");

    // Build a doc with a TinyObjectMap root containing an ExternalRef.
    // (binary_codec encodes from the root, so we wrap the ExternalRef
    //  in a map slot.)
    auto src = make_doc(4096).get();
    auto& src_arena = HermesAccess::arena(src);

    auto* ref_p = arena_put<ExternalRef>(
        src_arena, ExternalRef::make(arena_id_t{0x123456}, 0xDEADBEEF)).get();
    auto ref_off = HermesAccess::offset_of(src, ref_p);

    auto src_root = src.make_tiny_map(2).get();
    src_root.put(0, AnyVal::from_offset(ref_off)).get();
    src.set_root(src_root);

    // Encode → bytes → decode.
    auto bytes = binary_encode(src).get();
    LOGOS_ASSERT(!bytes.empty(), "EXTREF-CODEC-001", "encoded bytes non-empty");

    auto dst = binary_decode(bytes.data(), bytes.size()).get();
    auto* dst_base = HermesAccess::base(dst);

    // Walk decoded doc: root → TinyMap → slot 0 → ExternalRef.
    auto dst_root_av = dst.root_object().tagged();
    LOGOS_ASSERT(dst_root_av.is_pointer(), "EXTREF-CODEC-002",
        "decoded root must be a pointer");
    TinyMapView dst_map(dst_root_av.to_offset(), dst.holder());
    auto slot0 = dst_map.get(uint8_t{0});
    LOGOS_ASSERT(slot0.is_pointer(), "EXTREF-CODEC-003",
        "decoded slot 0 must be a pointer to ExternalRef");

    auto* decoded_ref = slot0.as_ptr<ExternalRef>(dst_base);
    LOGOS_ASSERT(decoded_ref->arena_id() == arena_id_t{0x123456},
        "EXTREF-CODEC-004",
        "decoded arena_id mismatch: got {}", decoded_ref->arena_id().value);
    LOGOS_ASSERT(decoded_ref->obj_id() == 0xDEADBEEF, "EXTREF-CODEC-005",
        "decoded obj_id mismatch: got 0x{:x}", decoded_ref->obj_id());

    std::printf("  PASS\n");
}

static void test_publish_helpers() {
    std::printf("--- arena publish helpers (lir_arena_root_begin/publish/finalize) ---\n");

    InMemoryArenaPool pool;

    // Build a coremeta-like arena via the publish flow.
    auto coremeta_doc = make_doc(8192).get();
    auto builder = lir_arena_root_begin(coremeta_doc, "coremeta", {}).get();
    auto& arena = HermesAccess::arena(builder.doc);

    // Publish 3 ExternalRef objects (just as test payload).
    auto* p1 = arena_put<ExternalRef>(arena,
        ExternalRef::make(arena_id_t{0}, 100)).get();
    auto* p2 = arena_put<ExternalRef>(arena,
        ExternalRef::make(arena_id_t{0}, 200)).get();
    auto* p3 = arena_put<ExternalRef>(arena,
        ExternalRef::make(arena_id_t{0}, 300)).get();

    auto oid1 = arena_publish(builder,
        AnyVal::from_offset(HermesAccess::offset_of(builder.doc, p1))).get();
    auto oid2 = arena_publish(builder,
        AnyVal::from_offset(HermesAccess::offset_of(builder.doc, p2))).get();
    auto oid3 = arena_publish(builder,
        AnyVal::from_offset(HermesAccess::offset_of(builder.doc, p3))).get();

    // Slot 0 was null sentinel; real obj_ids start at 1.
    LOGOS_ASSERT(oid1 == 1, "PUBLISH-001", "first obj_id should be 1, got {}", oid1);
    LOGOS_ASSERT(oid2 == 2, "PUBLISH-002", "second obj_id should be 2, got {}", oid2);
    LOGOS_ASSERT(oid3 == 3, "PUBLISH-003", "third obj_id should be 3, got {}", oid3);

    // Reserve an obj_id (null entry).
    auto oid_reserved = arena_publish_reserved(builder).get();
    LOGOS_ASSERT(oid_reserved == 4, "PUBLISH-004",
        "reserved obj_id should be 4, got {}", oid_reserved);

    // Finalize: sets root, seals arena.
    auto root_off = lir_arena_root_finalize(builder).get();
    LOGOS_ASSERT(root_off.value() != 0, "PUBLISH-005",
        "root offset should be non-zero after finalize");
    LOGOS_ASSERT(builder.finalized, "PUBLISH-006", "builder should be finalized");
    LOGOS_ASSERT(coremeta_doc.is_sealed(), "PUBLISH-007",
        "arena should be sealed after finalize");

    // Now register with the private pool.
    auto handle = register_lir_arena(coremeta_doc, pool).get();
    LOGOS_ASSERT(handle.arena_id.is_valid(), "PUBLISH-REG-001",
        "registered arena_id should be valid");
    LOGOS_ASSERT(handle.name == "coremeta", "PUBLISH-REG-002",
        "registered name mismatch: '{}'", handle.name);
    LOGOS_ASSERT(handle.depends_on.empty(), "PUBLISH-REG-003",
        "coremeta should have no deps");

    // Look up the published objects via the pool + directory.
    auto* mem = pool.get(handle.arena_id);
    LOGOS_ASSERT(mem != nullptr, "PUBLISH-REG-004", "pool lookup must succeed");

    auto root_view = LirArenaRootView(
        TinyMapView(root_off, mem));
    auto dir = root_view.directory();
    LOGOS_ASSERT(dir.size() == 5, "PUBLISH-REG-005",
        "directory size = 5 (slot 0 sentinel + 3 published + 1 reserved); got {}",
        dir.size());
    LOGOS_ASSERT(dir.get(0).is_null(), "PUBLISH-REG-006",
        "slot 0 must be null sentinel");
    LOGOS_ASSERT(dir.get(oid1).is_pointer(), "PUBLISH-REG-007",
        "slot {} must be a pointer", oid1);
    LOGOS_ASSERT(dir.get(oid_reserved).is_null(), "PUBLISH-REG-008",
        "reserved slot {} must be null", oid_reserved);

    // Resolve via directory: dir[oid1] → original ExternalRef.
    auto p1_resolved = dir.get(oid1);
    auto* p1_ptr = p1_resolved.as_ptr<ExternalRef>(mem->base());
    LOGOS_ASSERT(p1_ptr->obj_id() == 100, "PUBLISH-REG-009",
        "resolved obj_id mismatch: {}", p1_ptr->obj_id());

    std::printf("  PASS\n");
}

static void test_register_lir_arena_dep_chain() {
    std::printf("--- register_lir_arena with dep chain ---\n");

    InMemoryArenaPool pool;

    // Build coremeta first (no deps).
    auto coremeta = make_doc(4096).get();
    auto b1 = lir_arena_root_begin(coremeta, "coremeta", {}).get();
    lir_arena_root_finalize(b1).get();
    auto h1 = register_lir_arena(coremeta, pool).get();

    // Build alloc (depends on coremeta).
    auto alloc = make_doc(4096).get();
    auto b2 = lir_arena_root_begin(alloc, "alloc", {"coremeta"}).get();
    lir_arena_root_finalize(b2).get();
    auto h2 = register_lir_arena(alloc, pool).get();

    LOGOS_ASSERT(h2.depends_on.size() == 1, "DEP-CHAIN-001",
        "alloc must have 1 dep");
    LOGOS_ASSERT(h2.depends_on[0] == h1.arena_id, "DEP-CHAIN-002",
        "alloc's dep must resolve to coremeta's arena_id");

    // Build stdlib (depends on alloc + coremeta).
    auto stdlib = make_doc(4096).get();
    auto b3 = lir_arena_root_begin(stdlib, "stdlib", {"alloc", "coremeta"}).get();
    lir_arena_root_finalize(b3).get();
    auto h3 = register_lir_arena(stdlib, pool).get();

    LOGOS_ASSERT(h3.depends_on.size() == 2, "DEP-CHAIN-003",
        "stdlib must have 2 deps, got {}", h3.depends_on.size());
    LOGOS_ASSERT(h3.depends_on[0] == h2.arena_id, "DEP-CHAIN-004",
        "stdlib's first dep must be alloc");
    LOGOS_ASSERT(h3.depends_on[1] == h1.arena_id, "DEP-CHAIN-005",
        "stdlib's second dep must be coremeta");

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// Phase 2.A: cross-arena resolution helpers
// ---------------------------------------------------------------------------

static void test_cross_arena_resolve() {
    std::printf("--- cross-arena ExternalRef resolution via ArenaPool ---\n");

    InMemoryArenaPool pool;

    // Build provider arena ("coremeta"): allocate 2 ExternalRef payload
    // objects + publish them so they have obj_ids.
    auto provider = make_doc(4096).get();
    auto pb = lir_arena_root_begin(provider, "coremeta", {}).get();
    auto& parena = HermesAccess::arena(pb.doc);

    // Target objects — use ExternalRef instances themselves as test payload
    // (we're testing the resolve mechanism, not what's pointed at).
    auto* t1 = arena_put<ExternalRef>(parena,
        ExternalRef::make(arena_id_t{0}, 0xAAAA)).get();
    auto* t2 = arena_put<ExternalRef>(parena,
        ExternalRef::make(arena_id_t{0}, 0xBBBB)).get();

    auto oid_t1 = arena_publish(pb,
        AnyVal::from_offset(HermesAccess::offset_of(pb.doc, t1))).get();
    auto oid_t2 = arena_publish(pb,
        AnyVal::from_offset(HermesAccess::offset_of(pb.doc, t2))).get();

    lir_arena_root_finalize(pb).get();
    auto provider_handle = register_lir_arena(provider, pool).get();
    auto provider_aid = provider_handle.arena_id;

    // Build consumer arena: contains ExternalRefs pointing INTO provider.
    auto consumer = make_doc(4096).get();
    auto& carena = HermesAccess::arena(consumer);

    auto* cross1 = arena_put<ExternalRef>(carena,
        ExternalRef::make(provider_aid, oid_t1)).get();
    auto* cross2 = arena_put<ExternalRef>(carena,
        ExternalRef::make(provider_aid, oid_t2)).get();

    // Direct resolve via API.
    {
        auto r1 = resolve_external_ref(*cross1, pool);
        LOGOS_ASSERT(r1.ok(), "RESOLVE-001",
            "cross1 must resolve (provider registered, oid valid)");
        LOGOS_ASSERT(r1.mem == provider.holder(), "RESOLVE-002",
            "resolved mem must be provider's MemHolder");

        auto* target_obj = reinterpret_cast<const ExternalRef*>(
            r1.mem->base() + r1.offset.value());
        LOGOS_ASSERT(target_obj->obj_id() == 0xAAAA, "RESOLVE-003",
            "resolved obj_id must be 0xAAAA, got 0x{:x}", target_obj->obj_id());
    }
    {
        auto r2 = resolve_external_ref(*cross2, pool);
        LOGOS_ASSERT(r2.ok(), "RESOLVE-004", "cross2 must resolve");
        auto* target_obj = reinterpret_cast<const ExternalRef*>(
            r2.mem->base() + r2.offset.value());
        LOGOS_ASSERT(target_obj->obj_id() == 0xBBBB, "RESOLVE-005",
            "resolved obj_id must be 0xBBBB");
    }

    // Failure modes.
    {
        auto bad_aid = ExternalRef::make(arena_id_t{0xDEAD}, 1);
        auto rb = resolve_external_ref(bad_aid, pool);
        LOGOS_ASSERT(!rb.ok(), "RESOLVE-FAIL-001",
            "unknown arena_id must fail to resolve");
    }
    {
        auto bad_oid = ExternalRef::make(provider_aid, 0xDEAD);
        auto rb = resolve_external_ref(bad_oid, pool);
        LOGOS_ASSERT(!rb.ok(), "RESOLVE-FAIL-002",
            "out-of-range obj_id must fail to resolve");
    }
    {
        auto null_oid = ExternalRef::make(provider_aid, 0);
        auto rb = resolve_external_ref(null_oid, pool);
        LOGOS_ASSERT(!rb.ok(), "RESOLVE-FAIL-003",
            "obj_id 0 (invariant #13: invalid sentinel) must fail to resolve");
    }

    // Detection + resolve-if-external on a generic AnyVal.
    {
        auto av_cross = AnyVal::from_offset(HermesAccess::offset_of(consumer, cross1));
        LOGOS_ASSERT(is_external_ref_av(av_cross, HermesAccess::base(consumer)),
            "RESOLVE-DETECT-001", "is_external_ref_av(cross1) should be true");

        auto opt = resolve_if_external(av_cross, HermesAccess::base(consumer), pool);
        LOGOS_ASSERT(opt.has_value(), "RESOLVE-DETECT-002",
            "resolve_if_external should succeed for cross1");
    }
    {
        // A non-ExternalRef object: e.g., the LirArenaRoot map itself.
        auto av_root = AnyVal::from_offset(arena_offset_t{
            reinterpret_cast<const DocumentHeader*>(
                HermesAccess::base(consumer))->root_offset});
        // Consumer has no root, so this is NULL — should not be detected.
        LOGOS_ASSERT(!is_external_ref_av(av_root, HermesAccess::base(consumer)),
            "RESOLVE-DETECT-003",
            "is_external_ref_av on non-pointer / null should be false");
        LOGOS_ASSERT(!resolve_if_external(av_root, HermesAccess::base(consumer), pool)
                        .has_value(),
            "RESOLVE-DETECT-004",
            "resolve_if_external on non-ExternalRef should return nullopt");
    }

    std::printf("  PASS\n");
}

int main() {
    // Phase 0 tests (ArenaPool).
    test_register_lookup_drop();
    test_arena_id_not_reused();
    test_name_reuse_after_unregister();
    test_global_pool_singleton();

    // Phase 1.A tests (ExternalRef + LirArenaRoot infrastructure).
    test_external_ref_layout();
    test_external_ref_arena_roundtrip();
    test_lir_arena_root();

    // Phase 1.B tests (binary_codec + publish + register).
    test_external_ref_binary_codec_roundtrip();
    test_publish_helpers();
    test_register_lir_arena_dep_chain();

    // Phase 2.A tests (cross-arena resolve).
    test_cross_arena_resolve();

    std::printf("All ArenaPool + Phase 1.A/1.B + 2.A tests passed\n");
    return 0;
}
