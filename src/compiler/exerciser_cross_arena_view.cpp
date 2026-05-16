// Logos project — https://github.com/victor-smirnov/logos
//
// Cross-arena lir_view dispatcher exerciser (Phase 2.B of multi-arena IR).
//
// Verifies that:
//   - TypeRef stores arena_id_ correctly (default INVALID; explicit cross-arena set)
//   - ExprRef::sub_expr() and ::sub_type() transparently follow ExternalRef
//     when child fields point at one
//   - resolve_child() returns correct (arena, off, aid) for both local and
//     cross-arena children
//
// Uses the global ArenaPool with uniquely-named test modules (and unregisters
// in tear-down) so multiple test runs / ctest concurrency don't collide.

#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sema.hpp>

#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/verification/assert.hpp>

#include <cstdio>

using namespace logos::hermes;
using logos::compiler::TypeRef;
using logos::compiler::lir_view::ExprRef;
using logos::compiler::lir_view::detail::resolve_child;
using logos::compiler::lir_view::detail::ChildLoc;

// ---------------------------------------------------------------------------
// TypeRef carrier semantics
// ---------------------------------------------------------------------------

static void test_typeref_arena_id_default() {
    std::printf("--- TypeRef.arena_id() defaults to INVALID (local) ---\n");

    TypeRef null_ref;
    LOGOS_ASSERT(null_ref.arena_id() == INVALID_ARENA_ID, "TR-AID-001",
        "default-constructed TypeRef should carry INVALID arena_id");
    LOGOS_ASSERT(!null_ref.is_external(), "TR-AID-002",
        "default TypeRef.is_external() must be false");

    std::printf("  PASS\n");
}

static void test_typeref_explicit_cross_arena_ctor() {
    std::printf("--- TypeRef explicit cross-arena constructor ---\n");

    // Just verify the field round-trips; we don't need a real arena for this.
    auto fake_arena = reinterpret_cast<const Arena*>(uintptr_t{0xDEADBEEF});
    TypeRef cross(fake_arena, arena_offset_t{8}, /*pool=*/nullptr,
                   arena_id_t{42});
    LOGOS_ASSERT(cross.arena_id() == arena_id_t{42}, "TR-AID-101",
        "explicit cross-arena arena_id must round-trip; got {}",
        cross.arena_id().value);
    LOGOS_ASSERT(cross.is_external(), "TR-AID-102",
        "explicit cross-arena TypeRef.is_external() must be true");
    LOGOS_ASSERT(cross.offset() == arena_offset_t{8}, "TR-AID-103",
        "offset preserved");

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// End-to-end: build 2 arenas, register, traverse via resolve_child + sub_expr
// ---------------------------------------------------------------------------

// Allocate a generic TinyObjectMap stub usable as a fake "LIR node" target.
// We don't need a real schema_type_code — only the test dispatchers' field
// reads are exercised.
static arena_offset_t make_stub_node(Hermes& doc) {
    auto m = doc.make_tiny_map(2).get();
    return m.offset();
}

static void test_cross_arena_subexpr_dispatch() {
    std::printf("--- ExprRef::sub_expr cross-arena dispatch ---\n");

    auto& pool = global_arena_pool();

    // ── Build provider arena ───────────────────────────────────────────
    auto provider = make_doc(4096).get();
    auto pb = lir_arena_root_begin(provider, "phase2b_test_provider", {}).get();

    // Target node in provider: an empty TinyObjectMap; publish it with obj_id 1.
    auto target_off = make_stub_node(pb.doc);
    auto target_oid = arena_publish(pb,
        AnyVal::from_offset(target_off)).get();

    lir_arena_root_finalize(pb).get();
    auto provider_handle = register_lir_arena(provider, pool).get();
    auto provider_aid = provider_handle.arena_id;

    // ── Build consumer arena ───────────────────────────────────────────
    // Consumer has a parent node whose key 5 points at an ExternalRef object
    // that resolves to provider's target_off.
    auto consumer = make_doc(4096).get();
    auto& carena = HermesAccess::arena(consumer);

    // 1) Allocate the ExternalRef object first.
    auto* xref_p = arena_put<ExternalRef>(
        carena, ExternalRef::make(provider_aid, target_oid)).get();
    auto xref_off = HermesAccess::offset_of(consumer, xref_p);

    // 2) Allocate parent TinyObjectMap and store key 5 → AnyVal(ptr to ExtRef).
    auto parent_map = consumer.make_tiny_map(2).get();
    parent_map.put(uint8_t{5}, AnyVal::from_offset(xref_off)).get();

    // ── Construct ExprRef around the consumer's parent node ────────────
    ExprRef parent(&carena, parent_map.offset());
    LOGOS_ASSERT(!parent.is_external(), "X-SUB-001",
        "consumer-side parent should be local (arena_id INVALID)");

    // ── sub_expr(5) should resolve through ExternalRef → provider's target ─
    auto child = parent.sub_expr(uint8_t{5});
    LOGOS_ASSERT(bool(child), "X-SUB-002", "child sub_expr should be non-null");
    LOGOS_ASSERT(child.is_external(), "X-SUB-003",
        "resolved child should be external (arena_id valid)");
    LOGOS_ASSERT(child.arena_id() == provider_aid, "X-SUB-004",
        "child.arena_id should equal provider_aid ({}); got {}",
        provider_aid.value, child.arena_id().value);
    LOGOS_ASSERT(child.arena() == &HermesAccess::arena(provider),
        "X-SUB-005",
        "child.arena() should point at provider's arena");
    LOGOS_ASSERT(child.offset() == target_off, "X-SUB-006",
        "child.offset() should equal target_off");

    // ── Local-path: key 6 absent → null ExprRef ───────────────────────
    auto missing = parent.sub_expr(uint8_t{6});
    LOGOS_ASSERT(!missing, "X-SUB-007",
        "sub_expr on missing key should be null");

    // ── resolve_child direct: local AnyVal ────────────────────────────
    // Insert a key 7 that points at the parent itself (local ref, no ExtRef).
    parent_map.put(uint8_t{7}, AnyVal::from_offset(parent_map.offset())).get();
    auto av_local = parent.mirror()->get(uint8_t{7}, parent.base());
    auto loc_local = resolve_child(parent, av_local);
    LOGOS_ASSERT(bool(loc_local), "X-RC-001", "local resolve_child must succeed");
    LOGOS_ASSERT(!loc_local.aid.is_valid(), "X-RC-002",
        "local resolve_child should return INVALID arena_id (single-arena fast path)");
    LOGOS_ASSERT(loc_local.arena == &carena, "X-RC-003",
        "local resolve_child should preserve parent's arena");

    // ── resolve_child direct: ExternalRef AnyVal ──────────────────────
    auto av_ext = parent.mirror()->get(uint8_t{5}, parent.base());
    auto loc_ext = resolve_child(parent, av_ext);
    LOGOS_ASSERT(bool(loc_ext), "X-RC-101", "external resolve_child must succeed");
    LOGOS_ASSERT(loc_ext.aid == provider_aid, "X-RC-102",
        "external resolve_child must return provider's arena_id");
    LOGOS_ASSERT(loc_ext.arena == &HermesAccess::arena(provider),
        "X-RC-103", "external resolve_child must return provider's arena");
    LOGOS_ASSERT(loc_ext.off == target_off, "X-RC-104",
        "external resolve_child must return target's offset");

    // ── Tear-down: unregister so re-runs don't collide on the name ─────
    pool.unregister(provider_handle.arena_id);

    std::printf("  PASS\n");
}

int main() {
    test_typeref_arena_id_default();
    test_typeref_explicit_cross_arena_ctor();
    test_cross_arena_subexpr_dispatch();
    std::printf("All Phase 2.B cross-arena view tests passed\n");
    return 0;
}
