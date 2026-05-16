// Logos project — https://github.com/victor-smirnov/logos
//
// ArenaPool exerciser (Phase 0 of multi-arena IR refactor).
// Tests: register / lookup / unregister / refcount semantics / arena_id
// non-reuse / dep validation.

#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/arena_string.hpp>
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

    std::printf("All ArenaPool + Phase 1.A tests passed\n");
    return 0;
}
