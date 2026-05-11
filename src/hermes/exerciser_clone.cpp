// Logos project — https://github.com/victor-smirnov/logos
//
// Exerciser for hermes::clone() — step-1 dispatch-based deep clone.

#include <logos/hermes/access.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/map.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <vector>

using namespace logos::hermes;

static void expect_stringify_equal(const Hermes& a, const Hermes& b, const char* label) {
    auto sa = stringify(a, false).get();
    auto sb = stringify(b, false).get();
    LOGOS_ASSERT(sa == sb, "HERMES-CLONE-001",
        "{}: stringify mismatch\n  src: {}\n  dst: {}", label, sa, sb);
}

static void test_clone_empty() {
    std::printf("--- clone: empty document ---\n");
    auto doc = make_doc().get();
    auto cloned = clone(doc).get();
    auto s = stringify(cloned, false).get();
    LOGOS_ASSERT(s == "null", "HERMES-CLONE-002",
        "Empty clone must stringify as 'null', got '{}'", s);
    std::printf("  empty: OK\n");
}

static void test_clone_integer_root() {
    std::printf("--- clone: integer AnyVal root ---\n");
    auto doc = make_doc().get();
    // Integer root: must be a tagged object to appear as root (offset-based).
    auto arr = doc.make_array().get();
    doc.set_root(arr);
    arr.push_back(AnyVal::from_value(int32_t(12345))).get();

    auto cloned = clone(doc).get();
    expect_stringify_equal(doc, cloned, "integer root");
    std::printf("  integer root: OK\n");
}

static void test_clone_object_map_basic() {
    std::printf("--- clone: ObjectMap {k:42} ---\n");
    auto doc = make_doc().get();
    auto m = doc.make_object_map().get();
    doc.set_root(m);
    m.put("k", AnyVal::from_value(int32_t(42))).get();

    auto cloned = clone(doc).get();
    expect_stringify_equal(doc, cloned, "ObjectMap");

    auto* cm = HermesAccess::root<ObjectMap>(cloned);
    uint8_t* cb = HermesAccess::base(cloned);
    LOGOS_ASSERT(cm->size() == 1, "HERMES-CLONE-003", "map size");
    LOGOS_ASSERT(cm->get("k", cb).as_value<int32_t>() == 42, "HERMES-CLONE-003", "value");
    std::printf("  ObjectMap: OK\n");
}

static void test_clone_nested_array() {
    std::printf("--- clone: nested ObjectArray mixed types ---\n");
    auto doc = make_doc().get();
    auto outer = doc.make_array().get();
    doc.set_root(outer);

    outer.push_back(AnyVal::from_value(int32_t(7))).get();
    outer.push_back(AnyVal::from_value(int8_t(-3))).get();

    // Nested array of u32.
    auto inner = doc.make_array().get();
    for (int i = 0; i < 5; ++i) {
        inner.push_back(AnyVal::from_value(uint32_t(i * 11))).get();
    }
    // Re-fetch outer after growth.
    outer.push_back(inner.to_anyval()).get();

    auto cloned = clone(doc).get();
    expect_stringify_equal(doc, cloned, "nested array");
    std::printf("  nested array: OK\n");
}

static void test_clone_packed_size() {
    std::printf("--- clone: packed output ≤ src ---\n");
    auto doc = make_doc(65536).get();
    auto m = doc.make_object_map().get();
    doc.set_root(m);
    m.put("a", AnyVal::from_value(int32_t(1))).get();
    m.put("b", AnyVal::from_value(int32_t(2))).get();
    m.put("c", AnyVal::from_value(int32_t(3))).get();

    auto cloned = clone(doc).get();
    size_t src_used = HermesAccess::arena(doc).total_used();
    size_t dst_used = HermesAccess::arena(cloned).total_used();
    std::printf("  src_used=%zu, dst_used=%zu\n", src_used, dst_used);
    // Packed clone should not exceed src used bytes (garbage-free copy).
    LOGOS_ASSERT(dst_used <= src_used, "HERMES-CLONE-005",
        "Packed clone must not exceed src used bytes: dst={} src={}", dst_used, src_used);
    std::printf("  packed size: OK\n");
}

static void test_clone_param_tracking() {
    std::printf("--- clone: PARAM (tc=127) slot tracking ---\n");
    auto doc = make_doc().get();
    auto m = doc.make_object_map().get();
    doc.set_root(m);

    // PARAM AnyVal: raw = (index << 8) | 0xFF. index=5 → raw = 0x5FF.
    const uint32_t param_raw = (5u << 8) | 0xFFu;
    m.put("p", AnyVal::from_raw(param_raw)).get();

    std::vector<ParamSlot> params;
    auto cloned = clone(doc, &params).get();

    LOGOS_ASSERT(params.size() == 1, "HERMES-CLONE-006",
        "Expected 1 PARAM slot, got {}", params.size());
    LOGOS_ASSERT(params[0].value_index == 5, "HERMES-CLONE-006",
        "Expected value_index=5, got {}", params[0].value_index);
    uint8_t* base = HermesAccess::base(cloned);
    uint32_t read = *reinterpret_cast<const uint32_t*>(base + params[0].offset);
    LOGOS_ASSERT(read == param_raw, "HERMES-CLONE-006",
        "Expected slot raw=0x{:x}, got 0x{:x}", param_raw, read);
    std::printf("  PARAM tracking: OK (offset=%u, value_index=%u)\n",
                params[0].offset, params[0].value_index);
}

static void test_object_map_put_grow_stress() {
    std::printf("--- ObjectMap::put grow stress (tiny initial arena) ---\n");
    // Start with a small arena so put() triggers several grows; this would
    // previously lose entries because ++count_ wrote through a stale `this`.
    auto doc = make_doc(64).get();
    auto m = doc.make_object_map().get();
    doc.set_root(m);

    constexpr int N = 256;
    char buf[32];
    for (int i = 0; i < N; ++i) {
        std::snprintf(buf, sizeof(buf), "k%04d", i);
        m.put(buf, AnyVal::from_value(int32_t(i))).get();
    }

    auto* map = HermesAccess::root<ObjectMap>(doc);
    uint8_t* base = HermesAccess::base(doc);
    LOGOS_ASSERT(map->size() == static_cast<uint64_t>(N),
        "HERMES-CLONE-007", "Expected {} entries, got {}", N, map->size());
    for (int i = 0; i < N; ++i) {
        std::snprintf(buf, sizeof(buf), "k%04d", i);
        int32_t v = map->get(buf, base).as_value<int32_t>();
        LOGOS_ASSERT(v == i, "HERMES-CLONE-007",
            "Entry '{}' lost or corrupt (got {})", buf, v);
    }
    std::printf("  ObjectMap grow stress: OK (%d entries intact)\n", N);
}

static void test_clone_map_i32_anyval() {
    std::printf("--- clone: Map<i32,AnyVal> 3 entries (scalar, PARAM, ptr) ---\n");
    auto doc = make_doc().get();
    auto* m = MapI32AnyVal::create(HermesAccess::arena(doc), 8).get();
    // Make the map reachable as the document root.
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
    HermesAccess::set_root_offset(doc, logos::hermes::arena_offset_t(m_off));

    // 1) Inline scalar i32 value.
    m = reinterpret_cast<MapI32AnyVal*>(HermesAccess::base(doc) + m_off);
    bool ok = m->put(int32_t(1), AnyVal::from_value(int32_t(42)),
                     HermesAccess::base(doc));
    LOGOS_ASSERT(ok, "HERMES-MAP-001", "put #1 failed");

    // 2) PARAM marker (tc=127) value.
    const uint32_t param_raw = (7u << 8) | 0xFFu;
    m = reinterpret_cast<MapI32AnyVal*>(HermesAccess::base(doc) + m_off);
    ok = m->put(int32_t(2), AnyVal::from_raw(param_raw),
                HermesAccess::base(doc));
    LOGOS_ASSERT(ok, "HERMES-MAP-001", "put #2 failed");

    // 3) Pointer value to an ArenaString.
    auto* s = ArenaString::create(HermesAccess::arena(doc), "hello").get();
    uint32_t s_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(s) - HermesAccess::base(doc));
    m = reinterpret_cast<MapI32AnyVal*>(HermesAccess::base(doc) + m_off);
    ok = m->put(int32_t(3),
                AnyVal::from_offset(logos::hermes::arena_offset_t(s_off)),
                HermesAccess::base(doc));
    LOGOS_ASSERT(ok, "HERMES-MAP-001", "put #3 failed");

    // Sanity: stringify.
    auto ss = stringify(doc, false).get();
    std::printf("  src stringify: %s\n", ss.c_str());
    LOGOS_ASSERT(ss.find("<I32,AnyVal>") != std::string::npos,
        "HERMES-MAP-002", "Expected '<I32,AnyVal>' tag in stringify, got: {}", ss);
    LOGOS_ASSERT(ss.find("1:42") != std::string::npos,
        "HERMES-MAP-002", "Expected '1:42' entry, got: {}", ss);
    LOGOS_ASSERT(ss.find("3:\"hello\"") != std::string::npos,
        "HERMES-MAP-002", "Expected '3:\"hello\"' entry, got: {}", ss);

    // Clone with PARAM tracking.
    std::vector<ParamSlot> params;
    auto cloned = clone(doc, &params).get();

    // Equal stringification round-trip.
    auto sc = stringify(cloned, false).get();
    std::printf("  dst stringify: %s\n", sc.c_str());
    LOGOS_ASSERT(ss == sc, "HERMES-MAP-003",
        "Clone stringify mismatch:\n  src: {}\n  dst: {}", ss, sc);

    // PARAM slot bookkeeping.
    LOGOS_ASSERT(params.size() == 1, "HERMES-MAP-004",
        "Expected 1 PARAM slot, got {}", params.size());
    LOGOS_ASSERT(params[0].value_index == 7, "HERMES-MAP-004",
        "Expected value_index=7, got {}", params[0].value_index);
    uint8_t* cb = HermesAccess::base(cloned);
    uint32_t read = *reinterpret_cast<const uint32_t*>(cb + params[0].offset);
    LOGOS_ASSERT(read == param_raw, "HERMES-MAP-004",
        "PARAM slot raw mismatch: expected 0x{:x}, got 0x{:x}", param_raw, read);
    std::printf("  Map<i32,AnyVal> clone: OK (PARAM offset=%u, value_index=%u)\n",
                params[0].offset, params[0].value_index);
}

static void test_clone_inline_root_i32() {
    std::printf("--- clone: inline i32 root (AnyVal value-mode) ---\n");
    auto doc = make_doc().get();
    // Inline i32 root: write AnyVal raw directly to DocumentHeader.
    AnyVal v = AnyVal::from_value(int32_t(42));
    HermesAccess::set_root_offset(doc, logos::hermes::arena_offset_t(v.raw()));

    auto s = stringify(doc, false).get();
    LOGOS_ASSERT(s == "42", "HERMES-CLONE-008",
        "src inline-root i32 stringify expected '42', got '{}'", s);

    auto cloned = clone(doc).get();
    auto sc = stringify(cloned, false).get();
    LOGOS_ASSERT(sc == "42", "HERMES-CLONE-008",
        "cloned inline-root i32 stringify expected '42', got '{}'", sc);
    std::printf("  inline i32 root: OK\n");
}

static void test_clone_inline_root_param() {
    std::printf("--- clone: inline PARAM root ---\n");
    auto doc = make_doc().get();
    const uint32_t param_raw = (9u << 8) | 0xFFu;
    HermesAccess::set_root_offset(doc, logos::hermes::arena_offset_t(param_raw));

    std::vector<ParamSlot> params;
    auto cloned = clone(doc, &params).get();

    LOGOS_ASSERT(params.size() == 1, "HERMES-CLONE-009",
        "Expected 1 PARAM slot for inline PARAM root, got {}", params.size());
    LOGOS_ASSERT(params[0].offset == 0, "HERMES-CLONE-009",
        "Inline PARAM root slot offset must be 0, got {}", params[0].offset);
    LOGOS_ASSERT(params[0].value_index == 9, "HERMES-CLONE-009",
        "Expected value_index=9, got {}", params[0].value_index);
    uint8_t* cb = HermesAccess::base(cloned);
    uint32_t read = *reinterpret_cast<const uint32_t*>(cb + 0);
    LOGOS_ASSERT(read == param_raw, "HERMES-CLONE-009",
        "Inline PARAM root raw mismatch: expected 0x{:x}, got 0x{:x}",
        param_raw, read);
    std::printf("  inline PARAM root: OK\n");
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});
    logos::hermes::hermes_init();

    test_clone_empty();
    test_clone_integer_root();
    test_clone_object_map_basic();
    test_clone_nested_array();
    test_clone_packed_size();
    test_clone_param_tracking();
    test_object_map_put_grow_stress();
    test_clone_map_i32_anyval();
    test_clone_inline_root_i32();
    test_clone_inline_root_param();

    std::printf("\nAll hermes::clone exerciser tests passed.\n");
    logos::shutdown_sqlite_sink();
    return 0;
}
