// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/access.hpp>
#include <logos/hermes/document.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>

using namespace logos::hermes;

// ============================================================================
// Document creation and root access
// ============================================================================

static void test_document_create() {
    std::printf("--- Document create ---\n");

    auto doc = make_doc().get();

    LOGOS_ASSERT(!doc.has_root(), "HERMES-DOC-001",
        "New document must have no root");

    // Create a TinyObjectMap as root via View API.
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);

    LOGOS_ASSERT(doc.has_root(), "HERMES-DOC-001", "Document must have root after set_root");

    // Add data through the View.
    map.put(0, AnyVal::from_value(int32_t(42))).get();
    map.put(1, AnyVal::from_value(float(3.14f))).get();

    LOGOS_ASSERT(map.get(0).as_value<int32_t>() == 42, "HERMES-DOC-001", "");
    LOGOS_ASSERT(map.get(1).as_value<float>() == 3.14f, "HERMES-DOC-001", "");

    LOGOS_TRACE("hermes.doc.create", "status", "pass");
    std::printf("  Document create: OK\n");
}

static void test_document_nested_objects() {
    std::printf("--- Document nested objects ---\n");

    auto doc = make_doc().get();

    auto root = doc.make_tiny_map().get();
    doc.set_root(root);

    root.put(0, AnyVal::from_value(int32_t(100))).get();
    root.put(1, AnyVal::from_value(int32_t(200))).get();
    root.put(2, AnyVal::from_value(int32_t(300))).get();

    LOGOS_ASSERT(root.size() == 3, "HERMES-DOC-002", "Root must have 3 entries");
    LOGOS_ASSERT(root.get(0).as_value<int32_t>() == 100, "HERMES-DOC-002", "");
    LOGOS_ASSERT(root.get(1).as_value<int32_t>() == 200, "HERMES-DOC-002", "");
    LOGOS_ASSERT(root.get(2).as_value<int32_t>() == 300, "HERMES-DOC-002", "");

    LOGOS_TRACE("hermes.doc.nested", "status", "pass");
    std::printf("  Document nested objects: OK\n");
}

static void test_document_with_array_root() {
    std::printf("--- Document with array root ---\n");

    auto doc = make_doc().get();
    auto arr = doc.make_array().get();
    doc.set_root(arr);

    arr.push_back(AnyVal::from_value(int32_t(1))).get();
    arr.push_back(AnyVal::from_value(int32_t(2))).get();
    arr.push_back(AnyVal::from_value(int32_t(3))).get();

    // Add a string via pointer — use offset-based set.
    auto s = doc.make_string("test").get();
    arr.push_back(AnyVal{}).get();
    arr.slot(3)->set_pointer(s.ptr(), arr.ptr()->slot(3, HermesCtrAccess::base(doc)) ? HermesCtrAccess::base(doc) : HermesCtrAccess::base(doc));

    // Simpler: use set_offset on the AnyVal.
    arr.slot(3)->set_offset(s.offset());

    LOGOS_ASSERT(arr.size() == 4, "HERMES-DOC-002", "");
    LOGOS_ASSERT(arr.get(0).as_value<int32_t>() == 1, "HERMES-DOC-002", "");

    // Check string via slot.
    AnyVal* str_slot = arr.slot(3);
    LOGOS_ASSERT(str_slot->is_pointer(), "HERMES-DOC-002", "");
    LOGOS_ASSERT(*str_slot->as_ptr<ArenaString>(HermesCtrAccess::base(doc)) == "test", "HERMES-DOC-002", "");

    LOGOS_TRACE("hermes.doc.array_root", "status", "pass");
    std::printf("  Document with array root: OK\n");
}

// ============================================================================
// Compactification
// ============================================================================

static void test_compactify_simple() {
    std::printf("--- Compactify (simple) ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);

    map.put(0, AnyVal::from_value(int32_t(42))).get();
    map.put(5, AnyVal::from_value(float(2.5f))).get();
    map.put(10, AnyVal::from_value(int8_t(-1))).get();

    auto compact = compactify(doc).get();

    LOGOS_ASSERT(compact.has_root(), "HERMES-DOC-003", "Compacted doc must have root");

    auto* cmap = HermesCtrAccess::root<TinyObjectMap>(compact);
    uint8_t* cb = HermesCtrAccess::base(compact);
    LOGOS_ASSERT(cmap->size() == 3, "HERMES-DOC-003",
        "Compacted map must have 3 entries, got {}", cmap->size());
    LOGOS_ASSERT(cmap->get(0, cb).as_value<int32_t>() == 42, "HERMES-DOC-003", "");
    LOGOS_ASSERT(cmap->get(5, cb).as_value<float>() == 2.5f, "HERMES-DOC-003", "");
    LOGOS_ASSERT(cmap->get(10, cb).as_value<int8_t>() == -1, "HERMES-DOC-003", "");

    LOGOS_TRACE("hermes.doc.compactify", "status", "pass",
        "original_used", HermesCtrAccess::arena(doc).total_used(),
        "compact_used", HermesCtrAccess::arena(compact).total_used());
    std::printf("  Compactify (simple): OK (original %zu → compact %zu bytes)\n",
        HermesCtrAccess::arena(doc).total_used(), HermesCtrAccess::arena(compact).total_used());
}

static void test_compactify_array_with_values() {
    std::printf("--- Compactify (array with embedded values) ---\n");

    auto doc = make_doc().get();
    auto arr = doc.make_array().get();
    doc.set_root(arr);

    for (int i = 0; i < 20; ++i) {
        arr.push_back(AnyVal::from_value(int32_t(i * 100))).get();
    }

    auto compact = compactify(doc).get();

    auto* carr = HermesCtrAccess::root<ObjectArray>(compact);
    uint8_t* cb = HermesCtrAccess::base(compact);
    LOGOS_ASSERT(carr->size() == 20, "HERMES-DOC-003", "");
    for (int i = 0; i < 20; ++i) {
        LOGOS_ASSERT(carr->get(i, cb).as_value<int32_t>() == i * 100, "HERMES-DOC-003",
            "Compacted array[{}] must be {}", i, i * 100);
    }

    LOGOS_TRACE("hermes.doc.compactify_array", "status", "pass");
    std::printf("  Compactify (array): OK\n");
}

// ============================================================================
// Zero-copy serialization round-trip
// ============================================================================

static void test_zero_copy_round_trip() {
    std::printf("--- Zero-copy serialization round-trip ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);
    map.put(0, AnyVal::from_value(int32_t(42))).get();
    map.put(3, AnyVal::from_value(int32_t(99))).get();

    auto compact = compactify(doc).get();

    // Serialize to bytes.
    auto* data = HermesCtrAccess::base(compact);
    size_t size = HermesCtrAccess::arena(compact).total_used();
    LOGOS_ASSERT(data != nullptr, "HERMES-SERIAL-001", "");
    LOGOS_ASSERT(size > 0, "HERMES-SERIAL-001", "");

    // Deserialize (copy bytes).
    auto loaded = from_bytes_copy(data, size).get();

    LOGOS_ASSERT(loaded.has_root(), "HERMES-SERIAL-001", "Loaded doc must have root");

    auto* lmap = HermesCtrAccess::root<TinyObjectMap>(loaded);
    uint8_t* lb = HermesCtrAccess::base(loaded);
    LOGOS_ASSERT(lmap->size() == 2, "HERMES-SERIAL-001",
        "Loaded map must have 2 entries, got {}", lmap->size());
    LOGOS_ASSERT(lmap->get(0, lb).as_value<int32_t>() == 42, "HERMES-SERIAL-001", "");
    LOGOS_ASSERT(lmap->get(3, lb).as_value<int32_t>() == 99, "HERMES-SERIAL-001", "");

    LOGOS_TRACE("hermes.serial.zerocopy", "status", "pass", "bytes", size);
    std::printf("  Zero-copy round-trip: OK (%zu bytes)\n", size);
}

static void test_zero_copy_array_round_trip() {
    std::printf("--- Zero-copy array round-trip ---\n");

    auto doc = make_doc().get();
    auto arr = doc.make_array().get();
    doc.set_root(arr);

    for (int i = 0; i < 10; ++i) {
        arr.push_back(AnyVal::from_value(int32_t(i))).get();
    }

    auto compact = compactify(doc).get();
    auto* data = HermesCtrAccess::base(compact);
    size_t size = HermesCtrAccess::arena(compact).total_used();
    auto loaded = from_bytes_copy(data, size).get();

    auto* larr = HermesCtrAccess::root<ObjectArray>(loaded);
    uint8_t* lb = HermesCtrAccess::base(loaded);
    LOGOS_ASSERT(larr->size() == 10, "HERMES-SERIAL-001", "");
    for (int i = 0; i < 10; ++i) {
        LOGOS_ASSERT(larr->get(i, lb).as_value<int32_t>() == i, "HERMES-SERIAL-001", "");
    }

    LOGOS_TRACE("hermes.serial.zerocopy_array", "status", "pass");
    std::printf("  Zero-copy array round-trip: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes Layer 3-4: Document & Serialization Exerciser ===\n\n");

    test_document_create();
    test_document_nested_objects();
    test_document_with_array_root();
    test_compactify_simple();
    test_compactify_array_with_values();
    test_zero_copy_round_trip();
    test_zero_copy_array_round_trip();

    std::printf("\n=== All Layer 3-4 tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
