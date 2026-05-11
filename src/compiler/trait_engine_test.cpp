// Unit tests for trait_engine. Run via: ctest -L trait_engine
// or: ./build/src/compiler/trait_engine_test
//
// Phase 1: pure-data engine tests — no sema integration.

#include "trait_engine.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using logos::compiler::trait_engine::TraitEngine;
using logos::compiler::trait_engine::NO_IMPL;

static int g_failures = 0;

#define CHECK(expr) do {                                                    \
    if (!(expr)) {                                                          \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        ++g_failures;                                                       \
    }                                                                       \
} while (0)

static void test_direct_impls() {
    TraitEngine e;
    auto id = e.add_impl("Display", "i32");
    CHECK(id != NO_IMPL);
    CHECK(e.satisfies("Display", "i32"));
    CHECK(!e.satisfies("Display", "f32"));
    CHECK(!e.satisfies("Debug", "i32"));
    CHECK(e.resolve("Display", "i32") == id);
    CHECK(e.resolve("Display", "f32") == NO_IMPL);
}

static void test_dedup_direct_impls() {
    TraitEngine e;
    auto id1 = e.add_impl("Display", "i32");
    auto id2 = e.add_impl("Display", "i32");
    CHECK(id1 == id2);   // second add returns the existing id
}

static void test_blanket_impl() {
    TraitEngine e;
    // impl Copy for i32, impl<T: Copy> Clone for T   ⇒   i32 : Clone
    e.add_impl("Copy", "i32");
    e.add_blanket("Clone", "Copy");
    CHECK(e.satisfies("Clone", "i32"));
    CHECK(!e.satisfies("Clone", "Vec<i32>"));   // no Copy fact for Vec<i32>
}

static void test_blanket_chain() {
    TraitEngine e;
    e.add_impl("A", "i32");
    e.add_blanket("B", "A");
    e.add_blanket("C", "B");
    CHECK(e.satisfies("A", "i32"));
    CHECK(e.satisfies("B", "i32"));
    CHECK(e.satisfies("C", "i32"));
}

static void test_blanket_multi_bound_and() {
    TraitEngine e;
    // impl<T: A + B> C for T — T must satisfy both A and B.
    e.add_impl("A", "i32");
    e.add_impl("B", "i32");
    e.add_impl("A", "u32");
    // u32 satisfies A but NOT B
    e.add_blanket("C", std::vector<std::string>{"A", "B"});
    CHECK(e.satisfies("C", "i32"));
    CHECK(!e.satisfies("C", "u32"));
}

static void test_blanket_cycle_does_not_loop() {
    TraitEngine e;
    e.add_blanket("X", "Y");
    e.add_blanket("Y", "X");
    // Neither bound has a ground fact; satisfies must return false
    // for all types rather than recurse forever.
    CHECK(!e.satisfies("X", "i32"));
    CHECK(!e.satisfies("Y", "i32"));
}

static void test_auto_impl() {
    TraitEngine e;
    e.add_auto_impl("Send");
    CHECK(e.satisfies("Send", "i32"));
    CHECK(e.satisfies("Send", "Rc<i32>"));   // auto, no carve-out yet
    e.add_negative("Send", "Rc<i32>");
    CHECK(!e.satisfies("Send", "Rc<i32>"));  // negative beats auto
    CHECK(e.satisfies("Send", "i32"));        // unrelated still ok
}

static void test_shape_auto_impl_closures() {
    TraitEngine e;
    // Closure types tagged with prefix "Closure_". Sprint 5 keystone:
    // every closure implements Fn / FnMut / FnOnce.
    auto is_closure = [](std::string_view name) {
        return name.rfind("Closure_", 0) == 0;
    };
    e.add_shape_auto_impl("FnOnce", "closure", is_closure);
    e.add_shape_auto_impl("FnMut",  "closure", is_closure);
    e.add_shape_auto_impl("Fn",     "closure", is_closure);

    CHECK(e.satisfies("FnOnce", "Closure_abc"));
    CHECK(e.satisfies("FnMut",  "Closure_abc"));
    CHECK(e.satisfies("Fn",     "Closure_abc"));
    CHECK(!e.satisfies("Fn",    "i32"));

    // Blanket on top of shape-auto: any FnOnce gives ToFn.
    e.add_blanket("ToFn", "FnOnce");
    CHECK(e.satisfies("ToFn", "Closure_xyz"));
    CHECK(!e.satisfies("ToFn", "i32"));
}

static void test_clear_derived_invalidates_memo() {
    TraitEngine e;
    e.add_impl("T", "i32");
    CHECK(e.satisfies("T", "i32"));
    e.clear_derived();
    CHECK(e.satisfies("T", "i32"));   // direct facts persist
}

static void test_adding_fact_after_query_invalidates_memo() {
    TraitEngine e;
    CHECK(!e.satisfies("T", "i32"));    // memoise a "no"
    e.add_impl("T", "i32");
    CHECK(e.satisfies("T", "i32"));     // ... but the add must flush memo
}

static void test_trace_basic() {
    TraitEngine e;
    e.add_impl("Copy", "i32");
    e.add_blanket("Clone", "Copy");
    auto tr = e.trace_satisfies("Clone", "i32");
    CHECK(!tr.empty());
    bool has_blanket = false;
    bool has_direct = false;
    for (auto& s : tr) {
        if (s.find("blanket") != std::string::npos) has_blanket = true;
        if (s.find("direct")  != std::string::npos) has_direct  = true;
    }
    CHECK(has_blanket);
    CHECK(has_direct);
}

int main() {
    test_direct_impls();
    test_dedup_direct_impls();
    test_blanket_impl();
    test_blanket_chain();
    test_blanket_multi_bound_and();
    test_blanket_cycle_does_not_loop();
    test_auto_impl();
    test_shape_auto_impl_closures();
    test_clear_derived_invalidates_memo();
    test_adding_fact_after_query_invalidates_memo();
    test_trace_basic();
    if (g_failures == 0) {
        std::printf("trait_engine_test: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "trait_engine_test: %d failures\n", g_failures);
    return 1;
}
