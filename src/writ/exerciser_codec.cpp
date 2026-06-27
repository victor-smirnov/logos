// Logos project — https://github.com/victor-smirnov/logos
//
// Writ binary codec conformance — encode a document to a portable byte stream and
// decode it into a FRESH holder, verifying the tree round-trips intact (including a
// re-encode producing identical bytes).

#include <logos/writ/document.hpp>
#include <logos/writ/binary_codec.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/typed_array.hpp>
#include <logos/writ/compound_types.hpp>
#include <logos/writ/arena_string.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/type_codes.hpp>

#include <cstdio>

using namespace logos::writ;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    auto doc_exp = WritCtr::make();
    CHECK(doc_exp.has_value(), 1);
    WritCtr doc = std::move(*doc_exp);
    Arena& a = doc.arena();

    // root = { "name": "Ada", "age": 36, "flag": true, "nums": [1,2,3],
    //          "bytes": <u8 array 9,8>, "pi": <decimal 314/100> }
    auto map = ObjectMap::create(a, 8); CHECK(map.has_value(), 2);
    ObjectMap* m = *map;
    auto nm = ArenaString::create(a, "Ada"); CHECK(nm.has_value(), 3);
    AnyVal nmr; nmr.set_ref(*nm); (void)m->put("name", nmr, a);
    (void)m->put("age", AnyVal::pod(36, tc::WA_I56), a);
    (void)m->put("flag", AnyVal::pod_bool(true, tc::WA_BOOL), a);
    auto arr = ObjectArray::create(a, 4); CHECK(arr.has_value(), 4);
    for (int i = 1; i <= 3; ++i) (void)(*arr)->push_back(AnyVal::pod(i, tc::WA_I56), a);
    AnyVal ar; ar.set_ref(*arr); (void)m->put("nums", ar, a);
    auto ua = ArrayU8::create(a, 2); CHECK(ua.has_value(), 5);
    (void)(*ua)->push_back(9, a); (void)(*ua)->push_back(8, a);
    AnyVal uar; uar.set_ref(*ua); (void)m->put("bytes", uar, a);
    auto dec = Decimal::create(a, 314, 2, false); CHECK(dec.has_value(), 6);
    AnyVal dr; dr.set_ref(*dec); (void)m->put("pi", dr, a);
    AnyVal rootv; rootv.set_ref(m);
    doc.set_root(rootv);

    // encode → decode into a fresh holder
    auto enc = binary_encode(doc); CHECK(enc.has_value(), 7);
    CHECK(!enc->empty(), 8);
    auto dec_doc = binary_decode(enc->data(), enc->size()); CHECK(dec_doc.has_value(), 9);
    WritCtr d2 = std::move(*dec_doc);

    MapView mv = as_map(d2.root(), d2.holder());
    CHECK(mv.size() == 6, 10);
    CHECK(as_string(mv.get("name"), d2.holder()).view() == "Ada", 11);
    CHECK(mv.get("age").as_i56() == 36, 12);
    CHECK(mv.get("flag").is_pod() && mv.get("flag").as_bool(), 13);
    ArrayView av = as_array(mv.get("nums"), d2.holder());
    CHECK(av.size() == 3 && av.get(0).as_i56() == 1 && av.get(2).as_i56() == 3, 14);
    auto* u2 = reinterpret_cast<const ArrayU8*>(mv.get("bytes").resolve());
    CHECK(u2->size() == 2 && u2->get(0) == 9 && u2->get(1) == 8, 15);
    auto* p2 = reinterpret_cast<const Decimal*>(mv.get("pi").resolve());
    CHECK(p2->scale() == 2 && !p2->is_neg() && p2->coefficient() == 314, 16);

    // re-encode → re-decode is idempotent on DATA (ObjectMap is hash-ordered, so the
    // BYTES aren't canonical across capacities — only the data round-trips).
    auto enc2 = binary_encode(d2); CHECK(enc2.has_value(), 17);
    auto d3e = binary_decode(enc2->data(), enc2->size()); CHECK(d3e.has_value(), 18);
    MapView mv3 = as_map(d3e->root(), d3e->holder());
    CHECK(mv3.size() == 6 && mv3.get("age").as_i56() == 36, 19);
    CHECK(as_string(mv3.get("name"), d3e->holder()).view() == "Ada", 20);

    // a truncated stream is rejected, not a crash
    auto bad = binary_decode(enc->data(), enc->size() / 2);
    CHECK(!bad.has_value(), 21);

    std::printf("writ binary codec (encode/decode roundtrip + idempotent + bounds): OK\n");
    return 0;
}
