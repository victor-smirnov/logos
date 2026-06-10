// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 containers conformance — ArenaString (HString) + ObjectArray (HArray<HVal>)
// on the self-relative foundation. The hard case is GROWTH: the element buffer is
// reallocated and each at-rest AnyVal Ref must re-anchor to its new slot. Returns 0
// on success, else the first failing check code.

#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/typed_array.hpp>
#include <logos/hermes2/tiny_object_map.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/type_codes.hpp>

#include <cstdio>
#include <string_view>

using namespace logos::hermes2;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    auto arena_exp = Arena::make(ArenaMode::MultiChunk, 256);
    CHECK(arena_exp.has_value(), 1);
    Arena& arena = *arena_exp;

    // ── ArenaString: [vlen][utf8], tag = tc::STRING (130) ──────────────────────
    {
        auto s_exp = ArenaString::create(arena, "hello");
        CHECK(s_exp.has_value(), 10);
        ArenaString* s = *s_exp;
        CHECK(s->view() == "hello", 11);
        CHECK(s->length() == 5, 12);
        CHECK(s->arena_size() == 1 + 5, 13);                 // 1-byte vlen + 5 bytes
        CHECK(*s == std::string_view("hello"), 14);
        // tag is the Logos H2_STRING code, written in-band before the object
        CHECK(TypeTag::read_before(reinterpret_cast<const uint8_t*>(s)).type_code() == tc::STRING, 15);
        CHECK(tc::STRING == 130, 16);
        // a long string forces a multi-byte vlen prefix (≥249)
        std::string big(300, 'x');
        auto big_exp = ArenaString::create(arena, big);
        CHECK(big_exp.has_value() && (*big_exp)->length() == 300, 17);
        CHECK((*big_exp)->view() == big, 18);
    }

    // ── ObjectArray: Pods + Refs, GROWTH re-anchors at-rest Refs ───────────────
    {
        auto arr_exp = ObjectArray::create(arena, 2);        // small cap → forces grow
        CHECK(arr_exp.has_value(), 20);
        ObjectArray* arr = *arr_exp;
        CHECK(TypeTag::read_before(reinterpret_cast<const uint8_t*>(arr)).type_code() == tc::ARRAY, 21);
        CHECK(sizeof(ObjectArray) == 24, 22);

        // A string we'll reference from inside the array (the cross-object Ref case).
        auto str_exp = ArenaString::create(arena, "Ada");
        CHECK(str_exp.has_value(), 23);
        ArenaString* str = *str_exp;

        // push 50 elements (Pods) plus one Ref to the string — past cap=2 → several grows
        for (int i = 0; i < 50; ++i)
            CHECK(arr->push_back(AnyVal::pod(i * 100, tc::HA_I56), arena).has_value(), 24);
        AnyVal ref; ref.set_ref(str);
        CHECK(arr->push_back(ref, arena).has_value(), 25);
        CHECK(arr->size() == 51, 26);

        // After all the grows, every Pod survives...
        for (int i = 0; i < 50; ++i)
            CHECK(arr->get(static_cast<uint64_t>(i)).as_i56() == i * 100, 27);

        // ...and the Ref (re-anchored through every buffer move) still resolves to
        // the string — the whole point of self-relative + re-anchoring copy.
        AnyVal back = arr->get(50);
        CHECK(back.is_ref(), 28);
        auto* resolved = reinterpret_cast<const ArenaString*>(back.resolve());
        CHECK(resolved->view() == "Ada", 29);

        // set + pop
        arr->set(0, AnyVal::pod_bool(true, tc::HA_BOOL));
        CHECK(arr->get(0).is_pod() && arr->get(0).as_bool(), 30);
        arr->pop_back();
        CHECK(arr->size() == 50, 31);

        // nested array as an element (Ref to a child ObjectArray)
        auto child_exp = ObjectArray::create(arena, 1);
        CHECK(child_exp.has_value(), 32);
        CHECK((*child_exp)->push_back(AnyVal::pod(7, tc::HA_I56), arena).has_value(), 33);
        AnyVal child_ref; child_ref.set_ref(*child_exp);
        CHECK(arr->push_back(child_ref, arena).has_value(), 34);
        AnyVal cb = arr->get(arr->size() - 1);
        auto* child = reinterpret_cast<const ObjectArray*>(cb.resolve());
        CHECK(child->get(0).as_i56() == 7, 35);
    }

    // ── TypedArray<T>: plain packed elements, memcpy growth ────────────────────
    {
        auto a_exp = ArrayU8::create(arena, 2);
        CHECK(a_exp.has_value(), 40);
        ArrayU8* a = *a_exp;
        CHECK(TypeTag::read_before(reinterpret_cast<const uint8_t*>(a)).type_code() == tc::ARRAY_U8, 41);
        CHECK(sizeof(ArrayU8) == 24, 42);
        for (int i = 0; i < 50; ++i)
            CHECK(a->push_back(static_cast<uint8_t>(i), arena).has_value(), 43);
        CHECK(a->size() == 50, 44);
        for (int i = 0; i < 50; ++i) CHECK(a->get(static_cast<uint64_t>(i)) == static_cast<uint8_t>(i), 45);

        auto d_exp = ArrayF64::create(arena, 1);
        CHECK(d_exp.has_value(), 46);
        ArrayF64* d = *d_exp;
        CHECK(d->push_back(3.5, arena).has_value() && d->push_back(-1.25, arena).has_value(), 47);
        CHECK(d->get(0) == 3.5 && d->get(1) == -1.25, 48);
        CHECK(TypeTag::read_before(reinterpret_cast<const uint8_t*>(d)).type_code() == tc::ARRAY_F64, 49);
    }

    // ── TinyObjectMap: bitmap-indexed, key-order, fixed cap ────────────────────
    {
        auto m_exp = TinyObjectMap::create(arena, 8);
        CHECK(m_exp.has_value(), 50);
        TinyObjectMap* m = *m_exp;
        CHECK(TypeTag::read_before(reinterpret_cast<const uint8_t*>(m)).type_code() == tc::TINYMAP, 51);
        CHECK(sizeof(TinyObjectMap) == 16, 52);
        CHECK(m->capacity() == 8 && m->size() == 0, 53);

        // insert out of key order; values stay addressable by key
        (void)m->put(5, AnyVal::pod(500, tc::HA_I56), arena);
        (void)m->put(1, AnyVal::pod(100, tc::HA_I56), arena);
        (void)m->put(9, AnyVal::pod(900, tc::HA_I56), arena);
        CHECK(m->size() == 3, 54);
        CHECK(m->get(1).as_i56() == 100, 55);
        CHECK(m->get(5).as_i56() == 500, 56);
        CHECK(m->get(9).as_i56() == 900, 57);
        CHECK(m->has_key(5) && !m->has_key(7), 58);
        CHECK(m->get(7).is_null(), 59);

        // a Ref value survives the key-order shift inserts
        auto sx = ArenaString::create(arena, "v3");
        CHECK(sx.has_value(), 60);
        AnyVal rv; rv.set_ref(*sx);
        (void)m->put(3, rv, arena);                  // inserts between keys 1 and 5 → shifts 5,9 right
        CHECK(m->get(3).is_ref(), 61);
        CHECK(reinterpret_cast<const ArenaString*>(m->get(3).resolve())->view() == "v3", 62);
        CHECK(m->get(5).as_i56() == 500 && m->get(9).as_i56() == 900, 63);   // shifted, still correct

        // update existing key (no size change)
        (void)m->put(5, AnyVal::pod(555, tc::HA_I56), arena);
        CHECK(m->size() == 4 && m->get(5).as_i56() == 555, 64);

        // remove
        CHECK(m->remove(1) && !m->has_key(1) && m->size() == 3, 65);
        CHECK(m->get(9).as_i56() == 900, 66);   // survivors intact after left-shift
    }

    std::printf("hermes2 containers (string + array + typed_array + tinymap): OK\n");
    return 0;
}
