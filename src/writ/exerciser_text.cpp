// Logos project — https://github.com/victor-smirnov/logos
//
// Writ text conformance — stringify + text_parse round-trip on the JSON core.

#include <logos/writ/document.hpp>
#include <logos/writ/stringify.hpp>
#include <logos/writ/text_parser.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/type_codes.hpp>

#include <cstdio>
#include <string>

using namespace logos::writ;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

int main() {
    // ── parse → stringify is canonical (and idempotent) ────────────────────────
    {
        const char* src = "{\"name\": \"Ada\", \"age\": 36, \"ok\": true, \"nil\": null, \"xs\": [1, 2, 3]}";
        auto d = text_parse(src); CHECK(d.has_value(), 1);
        std::string s1 = stringify(*d);
        auto d2 = text_parse(s1); CHECK(d2.has_value(), 2);
        std::string s2 = stringify(*d2);
        CHECK(s1 == s2, 3);                                  // stringify∘parse is idempotent

        MapView m = as_map(d->root(), d->holder());
        CHECK(m.size() == 5, 4);
        CHECK(as_string(m.get("name"), d->holder()).view() == "Ada", 5);
        CHECK(m.get("age").as_i56() == 36, 6);
        CHECK(m.get("ok").is_pod() && m.get("ok").as_bool(), 7);
        CHECK(m.get("nil").is_null(), 8);
        ArrayView xs = as_array(m.get("xs"), d->holder());
        CHECK(xs.size() == 3 && xs.get(1).as_i56() == 2, 9);
    }

    // ── scalars + escapes ──────────────────────────────────────────────────────
    {
        auto d = text_parse("[true, false, null, -42, 3.5, \"a\\\"b\\n\"]");
        CHECK(d.has_value(), 10);
        ArrayView a = as_array(d->root(), d->holder());
        CHECK(a.size() == 6, 11);
        CHECK(a.get(0).as_bool() && !a.get(1).as_bool() && a.get(2).is_null(), 12);
        CHECK(a.get(3).as_i56() == -42, 13);
        CHECK(a.get(4).is_ref(), 14);                        // 3.5 → boxed f64
        CHECK(as_string(a.get(5), d->holder()).view() == std::string("a\"b\n"), 15);
        // a float round-trips through stringify back to a float value
        std::string s = stringify(*d);
        auto d2 = text_parse(s); CHECK(d2.has_value(), 16);
        ArrayView a2 = as_array(d2->root(), d2->holder());
        CHECK(a2.get(3).as_i56() == -42 && a2.get(4).is_ref(), 17);
    }

    // ── build a doc, stringify, re-parse → data preserved ──────────────────────
    {
        auto doc = WritCtr::make(); CHECK(doc.has_value(), 18);
        Arena& ar = doc->arena();
        auto arr = ObjectArray::create(ar, 2); CHECK(arr.has_value(), 19);
        (void)(*arr)->push_back(AnyVal::pod(7, tc::HA_I56), ar);
        (void)(*arr)->push_back(AnyVal::pod_bool(false, tc::HA_BOOL), ar);
        AnyVal rv; rv.set_ref(*arr); doc->set_root(rv);

        std::string s = stringify(*doc);
        CHECK(s == "[7, false]", 20);
        auto re = text_parse(s); CHECK(re.has_value(), 21);
        ArrayView a = as_array(re->root(), re->holder());
        CHECK(a.size() == 2 && a.get(0).as_i56() == 7 && !a.get(1).as_bool(), 22);
    }

    // ── malformed input is rejected, not a crash ───────────────────────────────
    CHECK(!text_parse("{\"a\": }").has_value(), 23);
    CHECK(!text_parse("[1, 2").has_value(), 24);
    CHECK(!text_parse("tru").has_value(), 25);

    std::printf("writ text (stringify + parse roundtrip): OK\n");
    return 0;
}
