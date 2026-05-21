// Logos project — https://github.com/victor-smirnov/logos
//
// Import-table + module-local arena_id resolution exerciser (multi-arena IR).
//
// Verifies the loader-side resolution path end-to-end on a private pool:
//   - build_import_table_blob → read_import_table_blob round-trips entries
//   - set_module_imports attaches (file_name, entries) to a registered arena
//   - find_arena_by_file maps a file basename → its global arena_id
//   - resolve_external_ref_local translates a MODULE-LOCAL arena_id (index into
//     the source module's import table) → global arena_id → directory[obj_id]
//   - the existing global resolve_external_ref is unchanged

#include <logos/hermes/any_val.hpp>
#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/import_table.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/verification/assert.hpp>

#include <cstdio>
#include <vector>

using namespace logos::hermes;

int main() {
    std::printf("--- import-table build/read round-trip ---\n");
    {
        std::vector<ImportEntry> in{{"liblogos-mem.a", ""}, {"liblogos-lang.a", "main"}};
        auto blob = build_import_table_blob("logos-std", in).get();
        auto out  = read_import_table_blob(blob.data(), blob.size()).get();
        // out is indexed by arena_id: [0] sentinel, [1..] entries.
        LOGOS_ASSERT(out.size() == 3, "IMP-RT-001", "expected 3 slots, got {}", out.size());
        LOGOS_ASSERT(out[0].file_name.empty(), "IMP-RT-002", "slot 0 must be sentinel");
        LOGOS_ASSERT(out[1].file_name == "liblogos-mem.a", "IMP-RT-003",
            "slot 1 file: {}", out[1].file_name);
        LOGOS_ASSERT(out[2].file_name == "liblogos-lang.a" && out[2].doc_name == "main",
            "IMP-RT-004", "slot 2 file/doc: {}/{}", out[2].file_name, out[2].doc_name);
        std::printf("    round-trip OK (3 slots)\n");
    }

    std::printf("--- module-local arena_id resolution ---\n");
    {
        InMemoryArenaPool pool;

        // Provider: publish a stub element at obj_id 1.
        auto provider = make_doc(4096).get();
        auto pb = lir_arena_root_begin(provider, "provider_mod", {}).get();
        auto target_off = pb.doc.make_tiny_map(2).get().offset();
        auto oid = arena_publish(pb, AnyVal::from_offset(target_off)).get();
        LOGOS_ASSERT(oid == 1, "IMP-RES-001", "first publish should be obj_id 1, got {}", oid);
        lir_arena_root_finalize(pb).get();
        auto ph = register_lir_arena(provider, pool).get();
        pool.set_module_imports(ph.arena_id, "libprovider.a", {});  // register its file

        LOGOS_ASSERT(pool.find_arena_by_file("libprovider.a") == ph.arena_id,
            "IMP-RES-002", "find_arena_by_file should return provider aid");
        LOGOS_ASSERT(!pool.find_arena_by_file("nope.a").is_valid(),
            "IMP-RES-003", "unknown file → INVALID");

        // Consumer: import table [0]=sentinel, [1]=provider.
        auto consumer = make_doc(4096).get();
        auto cb = lir_arena_root_begin(consumer, "consumer_mod", {}).get();
        lir_arena_root_finalize(cb).get();
        auto ch = register_lir_arena(consumer, pool).get();
        std::vector<ImportEntry> cimps;
        cimps.push_back(ImportEntry{});                    // arena_id 0 sentinel
        cimps.push_back(ImportEntry{"libprovider.a", ""}); // arena_id 1 → provider
        pool.set_module_imports(ch.arena_id, "libconsumer.a", std::move(cimps));

        // MODULE-LOCAL ExternalRef: arena_id 1 (consumer's import slot), obj_id 1.
        ExternalRef local_ref = ExternalRef::make(arena_id_t{1}, oid);
        auto r = resolve_external_ref_local(ch.arena_id, local_ref, pool);
        LOGOS_ASSERT(r.ok(), "IMP-RES-004", "local resolution should succeed");
        LOGOS_ASSERT(r.mem == pool.get(ph.arena_id), "IMP-RES-005",
            "resolved arena should be the provider");
        LOGOS_ASSERT(r.offset.value() == target_off.value(), "IMP-RES-006",
            "resolved offset should be the published element ({} vs {})",
            r.offset.value(), target_off.value());
        std::printf("    local arena_id 1 → provider, obj_id 1 → element OK\n");

        // Negative: a local arena_id with no import entry → unresolvable.
        auto bad = resolve_external_ref_local(
            ch.arena_id, ExternalRef::make(arena_id_t{2}, oid), pool);
        LOGOS_ASSERT(!bad.ok(), "IMP-RES-007", "out-of-range local arena_id must fail");
        std::printf("    out-of-range local arena_id → fail OK\n");

        // Repeat resolution → served from the lazy cache (same result).
        auto r2 = resolve_external_ref_local(ch.arena_id, local_ref, pool);
        LOGOS_ASSERT(r2.ok() && r2.mem == r.mem && r2.offset.value() == r.offset.value(),
            "IMP-RES-008", "cached re-resolution should match");
        std::printf("    cached re-resolution OK\n");
    }

    std::printf("--- lazy fill: target library loaded AFTER the importer ---\n");
    {
        InMemoryArenaPool pool;

        // Consumer registered first, importing "liblate.a" at arena_id 1 — but
        // that library is NOT loaded yet, so the slot stays null (unresolved).
        auto consumer = make_doc(4096).get();
        auto cb = lir_arena_root_begin(consumer, "consumer_mod", {}).get();
        lir_arena_root_finalize(cb).get();
        auto ch = register_lir_arena(consumer, pool).get();
        std::vector<ImportEntry> cimps;
        cimps.push_back(ImportEntry{});
        cimps.push_back(ImportEntry{"liblate.a", ""});
        pool.set_module_imports(ch.arena_id, "libconsumer.a", std::move(cimps));

        ExternalRef ref = ExternalRef::make(arena_id_t{1}, 1u);
        LOGOS_ASSERT(!resolve_external_ref_local(ch.arena_id, ref, pool).ok(),
            "IMP-LAZY-001", "unresolved before the target library is loaded");

        // Now load the late provider and register its file. The previously-null
        // cache slot resolves on the next access (lazy retry).
        auto provider = make_doc(4096).get();
        auto pb = lir_arena_root_begin(provider, "late_mod", {}).get();
        auto target_off = pb.doc.make_tiny_map(2).get().offset();
        auto oid = arena_publish(pb, AnyVal::from_offset(target_off)).get();
        lir_arena_root_finalize(pb).get();
        auto ph = register_lir_arena(provider, pool).get();
        pool.set_module_imports(ph.arena_id, "liblate.a", {});

        auto r = resolve_external_ref_local(ch.arena_id, ExternalRef::make(arena_id_t{1}, oid), pool);
        LOGOS_ASSERT(r.ok() && r.mem == pool.get(ph.arena_id), "IMP-LAZY-002",
            "resolves once the target library becomes available");
        std::printf("    null slot → resolves after target load OK\n");
    }

    std::printf("ALL IMPORT-TABLE TESTS PASSED\n");
    return 0;
}
