// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 document + compaction-to-blob + reload conformance. The headline test is
// RIGID RELOCATION: compactify packs the doc into a single segment; dumping its
// bytes and reloading them at a DIFFERENT address must still resolve every self-
// relative pointer (the block moved rigidly, so all internal deltas stay valid).

#include <logos/hermes2/document.hpp>
#include <logos/hermes2/clone.hpp>
#include <logos/hermes2/view.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/type_codes.hpp>

#include <cstdio>
#include <vector>

using namespace logos::hermes2;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    // ── Build a document: root = { "name": "Ada", "nums": [1, 2, 3] } ──────────
    auto doc_exp = HermesCtr::make();
    CHECK(doc_exp.has_value(), 1);
    HermesCtr doc = std::move(*doc_exp);
    Arena& a = doc.arena();
    CHECK(doc.root().is_null(), 2);                          // empty root initially

    auto map_exp = ObjectMap::create(a, 8);   CHECK(map_exp.has_value(), 3);
    ObjectMap* map = *map_exp;
    auto nm = ArenaString::create(a, "Ada");  CHECK(nm.has_value(), 4);
    AnyVal nmref; nmref.set_ref(*nm); (void)map->put("name", nmref, a);
    auto arr = ObjectArray::create(a, 4);     CHECK(arr.has_value(), 5);
    for (int i = 1; i <= 3; ++i) (void)(*arr)->push_back(AnyVal::pod(i, tc::HA_I56), a);
    AnyVal aref; aref.set_ref(*arr); (void)map->put("nums", aref, a);
    AnyVal rootv; rootv.set_ref(map);
    doc.set_root(rootv);
    CHECK(!doc.root().is_null() && doc.root().is_ref(), 6);

    // ── Compactify → single rigid segment ──────────────────────────────────────
    auto comp_exp = compactify(doc);
    CHECK(comp_exp.has_value(), 7);
    HermesCtr comp = std::move(*comp_exp);
    CHECK(comp.arena().chunk_count() == 1, 8);              // genuinely one segment
    {
        MapView m = as_map(comp.root(), comp.holder());
        CHECK(m.size() == 2, 9);
        CHECK(as_string(m.get("name"), comp.holder()).view() == "Ada", 10);
        ArrayView av = as_array(m.get("nums"), comp.holder());
        CHECK(av.size() == 3 && av.get(0).as_i56() == 1 && av.get(2).as_i56() == 3, 11);
    }

    // ── Dump the blob, reload at a DIFFERENT address (rigid relocation) ─────────
    std::vector<uint8_t> blob(comp.blob_data(), comp.blob_data() + comp.blob_size());
    CHECK(!blob.empty(), 12);

    auto re_exp = HermesCtr::from_bytes(blob.data(), blob.size());
    CHECK(re_exp.has_value(), 13);
    HermesCtr re = std::move(*re_exp);
    // The reloaded arena's base differs from the compact one — self-relative ptrs
    // must still resolve.
    CHECK(re.blob_data() != comp.blob_data(), 14);
    {
        MapView m = as_map(re.root(), re.holder());
        CHECK(m.size() == 2, 15);
        CHECK(as_string(m.get("name"), re.holder()).view() == "Ada", 16);
        ArrayView av = as_array(m.get("nums"), re.holder());
        CHECK(av.size() == 3, 17);
        CHECK(av.get(0).as_i56() == 1 && av.get(1).as_i56() == 2 && av.get(2).as_i56() == 3, 18);
    }

    std::printf("hermes2 document + compactify + blob reload (rigid relocation): OK\n");
    return 0;
}
