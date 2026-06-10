// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 clone / compaction conformance — deep_copy + DeepCopyState dedup. The two
// things that must hold: SHARED subgraphs stay shared in the clone (copied once),
// and CYCLES terminate (and stay cycles). Plus independence (a separate holder).

#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/clone.hpp>
#include <logos/hermes2/view.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/type_codes.hpp>

#include <cstdio>

using namespace logos::hermes2;

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

    cl.holder->unref();
    cc.holder->unref();
    src->unref();
    std::printf("hermes2 clone/compaction (shared + cycle + independence): OK\n");
    return 0;
}
