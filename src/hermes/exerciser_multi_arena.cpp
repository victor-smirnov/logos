// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes multi-arena conformance: the cross-module reference substrate that
// logosc's `.hermes0` LIR blobs ride on. Covers
//   • ExternalRef as an AnyVal Pod niche — encode/decode (incl. 24+32-bit maxima),
//     detection, and clone-preserves-verbatim (a cross-arena id must NOT be followed
//     by deep-copy, and a Pod never is);
//   • LirArenaRoot build → publish (DIRECTORY) + named exports (EXPORTS) → finalize →
//     compactify → register in a pool;
//   • resolve_external_ref + lookup_export across registered arenas;
//   • import-table blob roundtrip + resolve_external_ref_local through it.

#include <logos/hermes/external_ref.hpp>
#include <logos/hermes/arena_pool.hpp>
#include <logos/hermes/arena_publish.hpp>
#include <logos/hermes/lir_arena_root.hpp>
#include <logos/hermes/import_table.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/type_codes.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace logos::hermes;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

// Resolve a value-form Ref AnyVal to an ArenaString view (the published objects here
// are strings). `obj` is an absolute pointer into a live (pool-held) arena.
static std::string_view as_str(const uint8_t* obj) {
    return reinterpret_cast<const ArenaString*>(obj)->view();
}

int main() {
    // ── 1. ExternalRef Pod niche: encode/decode + detection ─────────────────────
    {
        AnyVal av = external_ref_av(arena_id_t{7}, 42);
        CHECK(is_external_ref_av(av), 1);
        CHECK(!av.is_ref() && av.is_pod(), 2);              // niche, not an allocation
        ExternalRef r = decode_external_ref(av);
        CHECK(r.aid == arena_id_t{7} && r.oid == 42, 3);

        // 24-bit arena_id + 32-bit obj_id maxima must round-trip exactly.
        AnyVal mx = external_ref_av(arena_id_t{0x00FFFFFF}, 0xFFFFFFFFu);
        ExternalRef rmx = decode_external_ref(mx);
        CHECK(rmx.aid == arena_id_t{0x00FFFFFF} && rmx.oid == 0xFFFFFFFFu, 4);

        // A plain int Pod / a Ref / null are NOT external refs.
        CHECK(!is_external_ref_av(AnyVal::pod(42, tc::HA_I56)), 5);
        CHECK(!is_external_ref_av(AnyVal{}), 6);
    }

    // ── 2. Build a producer module + register it ────────────────────────────────
    InMemoryArenaPool pool;                                 // private pool (hermetic)

    // Producer: publishes two strings, one of them exported by name.
    auto prod_exp = HermesCtr::make(); CHECK(prod_exp.has_value(), 10);
    HermesCtr prod = std::move(*prod_exp);
    uint32_t oid_alpha = 0, oid_beta = 0;
    {
        Arena& a = prod.arena();
        auto sa = ArenaString::create(a, "alpha"); CHECK(sa.has_value(), 11);
        auto sb = ArenaString::create(a, "beta");  CHECK(sb.has_value(), 12);

        auto bexp = lir_arena_root_begin(prod, "producer", {}); CHECK(bexp.has_value(), 13);
        ArenaPublishBuilder b = std::move(*bexp);
        AnyVal ra; ra.set_ref(*sa);
        AnyVal rb; rb.set_ref(*sb);
        auto oa = arena_publish_named(b, "sym_alpha", ra); CHECK(oa.has_value(), 14);
        auto ob = arena_publish(b, rb);                    CHECK(ob.has_value(), 15);
        oid_alpha = *oa; oid_beta = *ob;
        CHECK(oid_alpha == 1 && oid_beta == 2, 16);        // slot 0 = sentinel
        CHECK(lir_arena_root_finalize(b).has_value(), 17);
    }

    // Compact to a rigid blob (the .hermes0 shape) and register the compacted holder.
    auto compP_exp = compactify(prod); CHECK(compP_exp.has_value(), 18);
    HermesCtr compP = std::move(*compP_exp);
    auto hP = register_lir_arena(compP, pool); CHECK(hP.has_value(), 19);
    arena_id_t aid_P = hP->arena_id;
    CHECK(aid_P.is_valid(), 20);
    // Record the producer's own archive file name (for find_arena_by_file).
    pool.set_module_imports(aid_P, "liblogos-producer.a", {});

    // ── 3. resolve_external_ref + lookup_export ─────────────────────────────────
    {
        auto r = resolve_external_ref(ExternalRef{aid_P, oid_alpha}, pool);
        CHECK(r.ok(), 30);
        CHECK(as_str(r.obj) == "alpha", 31);

        auto r2 = resolve_external_ref(ExternalRef{aid_P, oid_beta}, pool);
        CHECK(r2.ok() && as_str(r2.obj) == "beta", 32);

        // obj_id 0 (sentinel) and out-of-range fail cleanly.
        CHECK(!resolve_external_ref(ExternalRef{aid_P, 0}, pool).ok(), 33);
        CHECK(!resolve_external_ref(ExternalRef{aid_P, 999}, pool).ok(), 34);
        CHECK(!resolve_external_ref(ExternalRef{arena_id_t{999}, 1}, pool).ok(), 35);

        // Named lookup finds the export's (arena_id, obj_id).
        auto ex = pool.lookup_export("sym_alpha");
        CHECK(ex.ok() && ex.arena_id == aid_P && ex.obj_id == oid_alpha, 36);
        CHECK(!pool.lookup_export("nope").ok(), 37);
    }

    // ── 4. clone/compactify preserves an ExternalRef Pod VERBATIM ───────────────
    // A doc whose root array holds [ExternalRef, int Pod, Ref string]. After a
    // compaction the ExternalRef must decode identically (NOT followed/rewritten).
    {
        auto d_exp = HermesCtr::make(); CHECK(d_exp.has_value(), 40);
        HermesCtr d = std::move(*d_exp);
        Arena& a = d.arena();
        auto arr = ObjectArray::create(a, 4); CHECK(arr.has_value(), 41);
        auto str = ArenaString::create(a, "payload"); CHECK(str.has_value(), 42);
        AnyVal sref; sref.set_ref(*str);
        (void)(*arr)->push_back(external_ref_av(arena_id_t{0x123456}, 0x89ABCDEFu), a);
        (void)(*arr)->push_back(AnyVal::pod(1234, tc::HA_I56), a);
        (void)(*arr)->push_back(sref, a);
        AnyVal root; root.set_ref(*arr);
        d.set_root(root);

        auto comp_exp = compactify(d); CHECK(comp_exp.has_value(), 43);
        HermesCtr comp = std::move(*comp_exp);
        ArrayView rv = as_array(comp.root(), comp.holder());
        CHECK(rv.size() == 3, 44);
        AnyVal e0 = rv.get(0);
        CHECK(is_external_ref_av(e0), 45);
        ExternalRef er = decode_external_ref(e0);
        CHECK(er.aid == arena_id_t{0x123456} && er.oid == 0x89ABCDEFu, 46);
        CHECK(rv.get(1).is_pod() && rv.get(1).as_i56() == 1234, 47);
        CHECK(as_string(rv.get(2), comp.holder()).view() == "payload", 48);
    }

    // ── 5. import-table blob roundtrip + resolve_external_ref_local ─────────────
    {
        // Build + parse a standalone import table for a consumer "modB" whose local
        // arena_id 1 imports the producer's archive.
        auto blob = build_import_table_blob("modB", {{"liblogos-producer.a", ""}});
        CHECK(blob.has_value(), 50);
        auto entries = read_import_table_blob(blob->data(), blob->size());
        CHECK(entries.has_value(), 51);
        CHECK(entries->size() == 2, 52);                    // [sentinel, the import]
        CHECK((*entries)[0].file_name.empty(), 53);
        CHECK((*entries)[1].file_name == "liblogos-producer.a", 54);

        // Register a (trivial) consumer module B and attach its import table.
        auto cb_exp = HermesCtr::make(); CHECK(cb_exp.has_value(), 55);
        HermesCtr consB = std::move(*cb_exp);
        {
            auto bexp = lir_arena_root_begin(consB, "modB", {}); CHECK(bexp.has_value(), 56);
            ArenaPublishBuilder b = std::move(*bexp);
            CHECK(lir_arena_root_finalize(b).has_value(), 57);
        }
        auto compB_exp = compactify(consB); CHECK(compB_exp.has_value(), 58);
        HermesCtr compB = std::move(*compB_exp);
        auto hB = register_lir_arena(compB, pool); CHECK(hB.has_value(), 59);
        arena_id_t aid_B = hB->arena_id;
        pool.set_module_imports(aid_B, "liblogos-modb.a", *entries);

        // An ExternalRef stored IN B uses the module-LOCAL arena_id 1 (→ producer).
        ExternalRef local_ref{arena_id_t{1}, oid_beta};
        auto rl = resolve_external_ref_local(aid_B, local_ref, pool);
        CHECK(rl.ok(), 60);
        CHECK(as_str(rl.obj) == "beta", 61);

        // A local arena_id with no import entry fails cleanly.
        CHECK(!resolve_external_ref_local(aid_B, ExternalRef{arena_id_t{9}, 1}, pool).ok(), 62);
    }

    std::printf("hermes multi-arena (ExternalRef niche + pool + publish/resolve + import table): OK\n");
    return 0;
}
