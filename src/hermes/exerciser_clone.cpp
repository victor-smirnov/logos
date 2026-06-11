// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes clone / compaction conformance — deep_copy + DeepCopyState dedup. The two
// things that must hold: SHARED subgraphs stay shared in the clone (copied once),
// and CYCLES terminate (and stay cycles). Plus independence (a separate holder).

#include <logos/hermes/mem_holder.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/typed_array.hpp>
#include <logos/hermes/map.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/type_codes.hpp>

#include <cstdio>

using namespace logos::hermes;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    auto src_exp = MemHolder::make();
    CHECK(src_exp.has_value(), 1);
    MemHolder* src = *src_exp;
    Arena& sa = src->arena();

    // ── SHARED subgraph: { "a": arr, "b": arr } — both keys → the SAME array ────
    auto arr_exp = ObjectArray::create(sa, 2);
    CHECK(arr_exp.has_value(), 2);
    ObjectArray* arr = *arr_exp;
    (void)arr->push_back(AnyVal::pod(10, tc::HA_I56), sa);
    (void)arr->push_back(AnyVal::pod(20, tc::HA_I56), sa);

    auto map_exp = ObjectMap::create(sa, 8);
    CHECK(map_exp.has_value(), 3);
    ObjectMap* map = *map_exp;
    AnyVal aref; aref.set_ref(arr);
    (void)map->put("a", aref, sa);
    AnyVal bref; bref.set_ref(arr);
    (void)map->put("b", bref, sa);

    AnyVal root; root.set_ref(map);

    // clone the whole tree into a fresh holder
    auto cl_exp = clone(root);
    CHECK(cl_exp.has_value(), 4);
    ClonedDoc cl = *cl_exp;
    CHECK(cl.holder != src, 5);                          // genuinely separate holder

    MapView cm = as_map(cl.root, cl.holder);
    CHECK(cm.size() == 2, 6);
    ArrayView ca = as_array(cm.get("a"), cl.holder);
    ArrayView cb = as_array(cm.get("b"), cl.holder);
    CHECK(ca.size() == 2 && ca.get(0).as_i56() == 10 && ca.get(1).as_i56() == 20, 7);
    CHECK(cb.size() == 2 && cb.get(1).as_i56() == 20, 8);
    // SHARED preserved: a and b resolve to the SAME cloned array (copied once)
    CHECK(ca.ptr() == cb.ptr(), 9);
    // ...and it is NOT the source array (independent copy)
    CHECK(reinterpret_cast<void*>(ca.ptr()) != reinterpret_cast<void*>(arr), 10);

    // independence: mutate the source AFTER cloning — clone is unaffected
    (void)arr->push_back(AnyVal::pod(30, tc::HA_I56), sa);
    CHECK(arr->size() == 3, 11);
    CHECK(ca.size() == 2, 12);                           // clone still 2 elements

    // ── CYCLE: an array referencing ITSELF ─────────────────────────────────────
    auto cyc_exp = ObjectArray::create(sa, 2);
    CHECK(cyc_exp.has_value(), 13);
    ObjectArray* cyc = *cyc_exp;
    (void)cyc->push_back(AnyVal::pod(99, tc::HA_I56), sa);
    AnyVal selfref; selfref.set_ref(cyc);
    (void)cyc->push_back(selfref, sa);                   // cyc[1] → cyc
    CHECK(cyc->get(1).resolve() == reinterpret_cast<uint8_t*>(cyc), 14);

    AnyVal cyc_root; cyc_root.set_ref(cyc);
    auto cc_exp = clone(cyc_root);                       // must terminate (dedup breaks the cycle)
    CHECK(cc_exp.has_value(), 15);
    ClonedDoc cc = *cc_exp;
    ArrayView cca = as_array(cc.root, cc.holder);
    CHECK(cca.size() == 2 && cca.get(0).as_i56() == 99, 16);
    // the clone's self-ref points at the CLONE (cycle preserved, not the source)
    CHECK(cca.get(1).resolve() == reinterpret_cast<uint8_t*>(cca.ptr()), 17);
    CHECK(cca.get(1).resolve() != reinterpret_cast<uint8_t*>(cyc), 18);

    // ── Leaf types survive a clone: decimal, typed array/map, typed-value, param ─
    {
        auto h2e = MemHolder::make(); CHECK(h2e.has_value(), 30);
        MemHolder* h2 = *h2e; Arena& a2 = h2->arena();

        auto dec = Decimal::create(a2, 12345, 2, true);   // -123.45
        CHECK(dec.has_value(), 31);
        auto ua  = ArrayU8::create(a2, 2); CHECK(ua.has_value(), 32);
        (void)(*ua)->push_back(7, a2); (void)(*ua)->push_back(8, a2);
        auto im  = MapI32::create(a2, 4); CHECK(im.has_value(), 33);
        (*im)->put(100, AnyVal::pod(1, tc::HA_I56)); (*im)->put(-3, AnyVal::pod(2, tc::HA_I56));
        auto tn  = ArenaString::create(a2, "Date"); CHECK(tn.has_value(), 34);
        AnyVal tnref; tnref.set_ref(*tn);
        auto tv  = TypedValue::create(a2, tnref, AnyVal::null(), AnyVal::pod(2024, tc::HA_I56));
        CHECK(tv.has_value(), 35);
        auto pn  = ArenaString::create(a2, "limit"); CHECK(pn.has_value(), 36);
        AnyVal pnref; pnref.set_ref(*pn);
        auto pm  = Parameter::create(a2, pnref, AnyVal::pod(50, tc::HA_I56));
        CHECK(pm.has_value(), 37);

        auto top = ObjectMap::create(a2, 8); CHECK(top.has_value(), 38);
        AnyVal r1; r1.set_ref(*dec); (void)(*top)->put("d", r1, a2);
        AnyVal r2; r2.set_ref(*ua);  (void)(*top)->put("u", r2, a2);
        AnyVal r3; r3.set_ref(*im);  (void)(*top)->put("m", r3, a2);
        AnyVal r4; r4.set_ref(*tv);  (void)(*top)->put("t", r4, a2);
        AnyVal r5; r5.set_ref(*pm);  (void)(*top)->put("p", r5, a2);
        AnyVal rootv; rootv.set_ref(*top);

        auto clv = clone(rootv); CHECK(clv.has_value(), 39);
        ClonedDoc cd = *clv;
        MapView cm2 = as_map(cd.root, cd.holder);

        auto* d2 = reinterpret_cast<const Decimal*>(cm2.get("d").resolve());
        CHECK(d2->scale() == 2 && d2->is_neg() && d2->coefficient() == 12345, 40);
        auto* u2 = reinterpret_cast<const ArrayU8*>(cm2.get("u").resolve());
        CHECK(u2->size() == 2 && u2->get(0) == 7 && u2->get(1) == 8, 41);
        auto* m2 = reinterpret_cast<const MapI32*>(cm2.get("m").resolve());
        CHECK(m2->get(100).as_i56() == 1 && m2->get(-3).as_i56() == 2, 42);
        auto* t2 = reinterpret_cast<const TypedValue*>(cm2.get("t").resolve());
        CHECK(reinterpret_cast<const ArenaString*>(t2->type_name.resolve())->view() == "Date", 43);
        CHECK(t2->init.as_i56() == 2024 && t2->params.is_null(), 44);
        auto* p2 = reinterpret_cast<const Parameter*>(cm2.get("p").resolve());
        CHECK(reinterpret_cast<const ArenaString*>(p2->name.resolve())->view() == "limit", 45);
        CHECK(p2->value.as_i56() == 50, 46);

        cd.holder->unref();
        h2->unref();
    }

    cl.holder->unref();
    cc.holder->unref();
    src->unref();
    std::printf("hermes clone/compaction (shared + cycle + independence + leaf types): OK\n");
    return 0;
}
