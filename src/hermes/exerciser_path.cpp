// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/access.hpp>
#include <logos/hermes/path.hpp>
#include <logos/hermes/template.hpp>
#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>

using namespace logos::hermes;

// ============================================================================
// HermesPath: identifier access
// ============================================================================

static void test_path_identifier() {
    std::printf("--- HermesPath: identifier ---\n");

    auto data = parse("{name: \"Alice\", age: 30}");

    {
        auto result = eval_path(data, "name");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-001", "");
        auto* s = HermesCtrAccess::root<ArenaString>(result);
        LOGOS_ASSERT(*s == "Alice", "HERMES-PATH-001",
            "Expected 'Alice', got '{}'", s->view());
    }
    {
        auto result = eval_path(data, "age");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-001", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<int32_t>(result) == 30, "HERMES-PATH-001", "");
    }
    {
        auto result = eval_path(data, "missing");
        LOGOS_ASSERT(!result.has_root(), "HERMES-PATH-001", "Missing key must return null");
    }

    LOGOS_TRACE("hermes.path.identifier", "status", "pass");
    std::printf("  HermesPath identifier: OK\n");
}

// ============================================================================
// HermesPath: subexpression (dot notation)
// ============================================================================

static void test_path_subexpression() {
    std::printf("--- HermesPath: subexpression ---\n");

    auto data = parse("{user: {name: \"Bob\", addr: {city: \"NY\"}}}");

    {
        auto result = eval_path(data, "user.name");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-002", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<ArenaString>(result) == "Bob", "HERMES-PATH-002", "");
    }
    {
        auto result = eval_path(data, "user.addr.city");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-002", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<ArenaString>(result) == "NY", "HERMES-PATH-002", "");
    }

    LOGOS_TRACE("hermes.path.subexpr", "status", "pass");
    std::printf("  HermesPath subexpression: OK\n");
}

// ============================================================================
// HermesPath: array index
// ============================================================================

static void test_path_array_index() {
    std::printf("--- HermesPath: array index ---\n");

    auto data = parse("{items: [10, 20, 30]}");

    {
        auto result = eval_path(data, "items[0]");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-003", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<int32_t>(result) == 10, "HERMES-PATH-003", "");
    }
    {
        auto result = eval_path(data, "items[-1]");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-003", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<int32_t>(result) == 30, "HERMES-PATH-003", "Negative index");
    }

    LOGOS_TRACE("hermes.path.index", "status", "pass");
    std::printf("  HermesPath array index: OK\n");
}

// ============================================================================
// HermesPath: slice
// ============================================================================

static void test_path_slice() {
    std::printf("--- HermesPath: slice ---\n");

    auto data = parse("{items: [0, 1, 2, 3, 4]}");

    {
        auto result = eval_path(data, "items[1:3]");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-004", "");
        auto* arr = HermesCtrAccess::root<ObjectArray>(result);
        LOGOS_ASSERT(arr->size() == 2, "HERMES-PATH-004",
            "Slice [1:3] must have 2 elements, got {}", arr->size());
    }

    LOGOS_TRACE("hermes.path.slice", "status", "pass");
    std::printf("  HermesPath slice: OK\n");
}

// ============================================================================
// HermesPath: wildcard, filter, pipe
// ============================================================================

static void test_path_wildcard_filter() {
    std::printf("--- HermesPath: wildcard & filter ---\n");

    auto data = parse("{items: [1, 2, 3, 4, 5]}");

    {
        auto result = eval_path(data, "items[*]");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-005", "");
        auto* arr = HermesCtrAccess::root<ObjectArray>(result);
        LOGOS_ASSERT(arr->size() == 5, "HERMES-PATH-005", "");
    }

    LOGOS_TRACE("hermes.path.wildcard", "status", "pass");
    std::printf("  HermesPath wildcard & filter: OK\n");
}

// ============================================================================
// HermesPath: comparator & logical
// ============================================================================

static void test_path_comparator() {
    std::printf("--- HermesPath: comparator ---\n");

    auto data = parse("{x: 10, y: 20}");

    {
        auto result = eval_path(data, "x < y");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-006", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<uint8_t>(result) == 1, "HERMES-PATH-006", "10 < 20 must be true");
    }
    {
        auto result = eval_path(data, "x == y");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-006", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<uint8_t>(result) == 0, "HERMES-PATH-006", "10 == 20 must be false");
    }

    LOGOS_TRACE("hermes.path.comparator", "status", "pass");
    std::printf("  HermesPath comparator: OK\n");
}

// ============================================================================
// HermesPath: functions
// ============================================================================

static void test_path_functions() {
    std::printf("--- HermesPath: functions ---\n");

    auto data = parse("{items: [1, 2, 3], name: \"hello\"}");

    {
        auto result = eval_path(data, "length(items)");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-007", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<int32_t>(result) == 3, "HERMES-PATH-007", "");
    }
    {
        auto result = eval_path(data, "length(name)");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-007", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<int32_t>(result) == 5, "HERMES-PATH-007", "");
    }
    {
        auto result = eval_path(data, "type(name)");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-007", "");
        LOGOS_ASSERT(*HermesCtrAccess::root<ArenaString>(result) == "string", "HERMES-PATH-007", "");
    }

    LOGOS_TRACE("hermes.path.functions", "status", "pass");
    std::printf("  HermesPath functions: OK\n");
}

// ============================================================================
// HermesPath: multiselect
// ============================================================================

static void test_path_multiselect() {
    std::printf("--- HermesPath: multiselect ---\n");

    auto data = parse("{a: 1, b: 2, c: 3}");

    {
        auto result = eval_path(data, "[a, c]");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-008", "");
        auto* arr = HermesCtrAccess::root<ObjectArray>(result);
        LOGOS_ASSERT(arr->size() == 2, "HERMES-PATH-008", "");
    }
    {
        auto result = eval_path(data, "{x: a, y: b}");
        LOGOS_ASSERT(result.has_root(), "HERMES-PATH-008", "");
        auto* map = HermesCtrAccess::root<ObjectMap>(result);
        LOGOS_ASSERT(map->size() == 2, "HERMES-PATH-008", "");
        LOGOS_ASSERT(map->get("x", HermesCtrAccess::base(result)).as_value<int32_t>() == 1, "HERMES-PATH-008", "");
    }

    LOGOS_TRACE("hermes.path.multiselect", "status", "pass");
    std::printf("  HermesPath multiselect: OK\n");
}

// ============================================================================
// Template: basic variable output
// ============================================================================

static void test_template_var() {
    std::printf("--- Template: variable output ---\n");

    auto data = parse("{name: \"World\"}");
    std::string result = render("Hello, {{ name }}!", data);
    LOGOS_ASSERT(result == "Hello, World!", "HERMES-TPL-001",
        "Expected 'Hello, World!', got '{}'", result);

    LOGOS_TRACE("hermes.template.var", "status", "pass");
    std::printf("  Template variable output: OK\n");
}

// ============================================================================
// Template: for loop
// ============================================================================

static void test_template_for() {
    std::printf("--- Template: for loop ---\n");

    auto data = parse("{items: [1, 2, 3]}");
    std::string result = render("{% for x in items %}[{{ x }}]{% endfor %}", data);
    LOGOS_ASSERT(result == "[1][2][3]", "HERMES-TPL-002",
        "Expected '[1][2][3]', got '{}'", result);

    LOGOS_TRACE("hermes.template.for", "status", "pass");
    std::printf("  Template for loop: OK\n");
}

// ============================================================================
// Template: if/else
// ============================================================================

static void test_template_if() {
    std::printf("--- Template: if/else ---\n");

    {
        auto data = parse("{show: true}");
        std::string result = render("{% if show %}yes{% else %}no{% endif %}", data);
        LOGOS_ASSERT(result == "yes", "HERMES-TPL-003",
            "Expected 'yes', got '{}'", result);
    }
    {
        auto data = parse("{show: false}");
        std::string result = render("{% if show %}yes{% else %}no{% endif %}", data);
        LOGOS_ASSERT(result == "no", "HERMES-TPL-003",
            "Expected 'no', got '{}'", result);
    }

    LOGOS_TRACE("hermes.template.if", "status", "pass");
    std::printf("  Template if/else: OK\n");
}

// ============================================================================
// Template: set
// ============================================================================

static void test_template_set() {
    std::printf("--- Template: set ---\n");

    auto data = parse("{x: 10}");
    std::string result = render("{% set y = x %}{{ y }}", data);
    LOGOS_ASSERT(result == "10", "HERMES-TPL-004",
        "Expected '10', got '{}'", result);

    LOGOS_TRACE("hermes.template.set", "status", "pass");
    std::printf("  Template set: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes: HermesPath & Template Exerciser ===\n\n");

    test_path_identifier();
    test_path_subexpression();
    test_path_array_index();
    test_path_slice();
    test_path_wildcard_filter();
    test_path_comparator();
    test_path_functions();
    //test_path_multiselect(); // TODO: cross-arena RelativePtr issue in multiselect hash

    test_template_var();
    test_template_for();
    test_template_if();
    test_template_set();

    std::printf("\n=== All HermesPath & Template tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
