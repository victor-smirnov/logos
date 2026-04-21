// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Exerciser for hermes::clone() — step-1 dispatch-based deep clone.

#include <logos/hermes/access.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>

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

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});
    logos::hermes::hermes_init();

    test_clone_empty();
    test_clone_integer_root();
    test_clone_object_map_basic();
    test_clone_nested_array();
    test_clone_packed_size();

    std::printf("\nAll hermes::clone exerciser tests passed.\n");
    logos::shutdown_sqlite_sink();
    return 0;
}
