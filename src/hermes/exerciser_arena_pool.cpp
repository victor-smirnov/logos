// Logos project — https://github.com/victor-smirnov/logos
//
// ArenaPool exerciser (Phase 0 of multi-arena IR refactor).
// Tests: register / lookup / unregister / refcount semantics / arena_id
// non-reuse / dep validation.

#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/mem_holder.hpp>
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

int main() {
    test_register_lookup_drop();
    test_arena_id_not_reused();
    test_name_reuse_after_unregister();
    test_global_pool_singleton();
    std::printf("All ArenaPool tests passed\n");
    return 0;
}
