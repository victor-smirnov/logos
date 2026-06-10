// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 containers conformance — ArenaString (HString) + ObjectArray (HArray<HVal>)
// on the self-relative foundation. The hard case is GROWTH: the element buffer is
// reallocated and each at-rest AnyVal Ref must re-anchor to its new slot. Returns 0
// on success, else the first failing check code.

#include <logos/hermes2/arena.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/object_array.hpp>
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

    std::printf("hermes2 containers (string + array): OK\n");
    return 0;
}
