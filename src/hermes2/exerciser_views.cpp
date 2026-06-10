// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes2 views conformance — OWNING views carrying the MemHolder refcount, and
// navigation that shares the holder across child views. Returns 0 on success.

#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/view.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/type_codes.hpp>

#include <cstdio>

using namespace logos::hermes2;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    auto h_exp = MemHolder::make();
    CHECK(h_exp.has_value(), 1);
    MemHolder* holder = *h_exp;            // initial ref (count 1)
    CHECK(holder->use_count() == 1, 2);

    // Build { "name": "Ada", "nums": [10, 20] } in the holder's arena.
    Arena& arena = holder->arena();
    auto map_exp = ObjectMap::create(arena);
    CHECK(map_exp.has_value(), 3);
    ObjectMap* map = *map_exp;

    auto name_exp = ArenaString::create(arena, "Ada");
    CHECK(name_exp.has_value(), 4);
    AnyVal nameref; nameref.set_ref(*name_exp);
    CHECK(map->put("name", nameref, arena).has_value(), 5);

    auto arr_exp = ObjectArray::create(arena, 2);
    CHECK(arr_exp.has_value(), 6);
    ObjectArray* arr = *arr_exp;
    CHECK(arr->push_back(AnyVal::pod(10, tc::HA_I56), arena).has_value(), 7);
    CHECK(arr->push_back(AnyVal::pod(20, tc::HA_I56), arena).has_value(), 8);
    AnyVal arrref; arrref.set_ref(arr);
    CHECK(map->put("nums", arrref, arena).has_value(), 9);

    // building objects in the arena does NOT touch the refcount
    CHECK(holder->use_count() == 1, 10);

    // ── Owning view: +1 ref while alive ────────────────────────────────────────
    {
        MapView mv(map, holder);
        CHECK(holder->use_count() == 2, 11);
        CHECK(mv.size() == 2 && mv.has("name") && !mv.has("x"), 12);

        // copy a view → another ref; scope exit → released
        {
            MapView mv2 = mv;
            CHECK(holder->use_count() == 3, 13);
        }
        CHECK(holder->use_count() == 2, 14);

        // navigate to a child string view (shares the holder → +1 while alive)
        {
            StringView sv = as_string(mv.get("name"), holder);
            CHECK(holder->use_count() == 3, 15);
            CHECK(sv.view() == "Ada", 16);
        }
        CHECK(holder->use_count() == 2, 17);

        // navigate to the child array, read elements, push a third (grows in-arena)
        {
            ArrayView av = as_array(mv.get("nums"), holder);
            CHECK(holder->use_count() == 3, 18);
            CHECK(av.size() == 2, 19);
            CHECK(av.get(0).as_i56() == 10 && av.get(1).as_i56() == 20, 20);
            CHECK(av.push_back(AnyVal::pod(30, tc::HA_I56)).has_value(), 21);
            CHECK(av.size() == 3 && av.get(2).as_i56() == 30, 22);
        }
        CHECK(holder->use_count() == 2, 23);

        // move a view → no refcount change
        MapView moved = std::move(mv);
        CHECK(holder->use_count() == 2, 24);
        CHECK(mv.is_null() && moved, 25);
    }
    // all views dropped → back to the single initial ref
    CHECK(holder->use_count() == 1, 26);

    holder->unref();   // releases the last ref → frees the holder + arena
    std::printf("hermes2 views (owning + navigation): OK\n");
    return 0;
}
