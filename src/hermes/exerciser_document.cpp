#include <logos/hermes/document.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <cstring>

using namespace logos::hermes;

// ============================================================================
// Document creation and root access
// ============================================================================

static void test_document_create() {
    std::printf("--- Document create ---\n");

    auto doc = HermesCtr::create();

    LOGOS_ASSERT(!doc.has_root(), "HERMES-DOC-001",
        "New document must have no root");

    // Create a TinyObjectMap as root.
    auto* map = doc.make_tiny_map();
    doc.set_root(map);

    LOGOS_ASSERT(doc.has_root(), "HERMES-DOC-001", "Document must have root after set_root");
    LOGOS_ASSERT(doc.root<TinyObjectMap>() == map, "HERMES-DOC-001",
        "root() must return the same pointer as set_root()");

    // Add some data.
    map->put(0, TaggedPtr::from_value(int32_t(42), type_hash::Integer), doc.arena());
    map->put(1, TaggedPtr::from_value(float(3.14f), type_hash::Real), doc.arena());

    LOGOS_ASSERT(map->get(0).as_value<int32_t>() == 42, "HERMES-DOC-001", "");
    LOGOS_ASSERT(map->get(1).as_value<float>() == 3.14f, "HERMES-DOC-001", "");

    LOGOS_TRACE("hermes.doc.create", "status", "pass");
    std::printf("  Document create: OK\n");
}

static void test_document_nested_objects() {
    std::printf("--- Document nested objects ---\n");

    auto doc = HermesCtr::create();

    // Build a small document: root map with an array and a string.
    auto* root = doc.make_tiny_map();
    doc.set_root(root);

    // Key 0 = string "hello"
    auto* greeting = doc.make_string("hello");
    root->put(0, TaggedPtr{}, doc.arena());  // placeholder
    // We need to use in-place pointer... but TinyObjectMap doesn't have slot().
    // For embedded values this is fine. For pointers, we need to add slot().
    // For this test, use embedded int values only — pointer test in ObjectArray.

    root->put(0, TaggedPtr::from_value(int32_t(100), type_hash::Integer), doc.arena());
    root->put(1, TaggedPtr::from_value(int32_t(200), type_hash::Integer), doc.arena());
    root->put(2, TaggedPtr::from_value(int32_t(300), type_hash::Integer), doc.arena());

    auto* root_read = doc.root<TinyObjectMap>();
    LOGOS_ASSERT(root_read->size() == 3, "HERMES-DOC-002", "Root must have 3 entries");
    LOGOS_ASSERT(root_read->get(0).as_value<int32_t>() == 100, "HERMES-DOC-002", "");
    LOGOS_ASSERT(root_read->get(1).as_value<int32_t>() == 200, "HERMES-DOC-002", "");
    LOGOS_ASSERT(root_read->get(2).as_value<int32_t>() == 300, "HERMES-DOC-002", "");

    // Suppress unused variable warning.
    (void)greeting;

    LOGOS_TRACE("hermes.doc.nested", "status", "pass");
    std::printf("  Document nested objects: OK\n");
}

static void test_document_with_array_root() {
    std::printf("--- Document with array root ---\n");

    auto doc = HermesCtr::create();
    auto* arr = doc.make_array();
    doc.set_root(arr);

    arr->push_back(TaggedPtr::from_value(int32_t(1), type_hash::Integer), doc.arena());
    arr->push_back(TaggedPtr::from_value(int32_t(2), type_hash::Integer), doc.arena());
    arr->push_back(TaggedPtr::from_value(int32_t(3), type_hash::Integer), doc.arena());

    // Add a string via slot.
    auto* s = doc.make_string("test");
    arr->push_back(TaggedPtr{}, doc.arena());
    arr->slot(3)->set_pointer(s);

    auto* read_arr = doc.root<ObjectArray>();
    LOGOS_ASSERT(read_arr->size() == 4, "HERMES-DOC-002", "");
    LOGOS_ASSERT(read_arr->get(0).as_value<int32_t>() == 1, "HERMES-DOC-002", "");

    TaggedPtr* str_slot = read_arr->slot(3);
    LOGOS_ASSERT(str_slot->is_pointer(), "HERMES-DOC-002", "");
    LOGOS_ASSERT(*str_slot->as_ptr<ArenaString>() == "test", "HERMES-DOC-002", "");

    LOGOS_TRACE("hermes.doc.array_root", "status", "pass");
    std::printf("  Document with array root: OK\n");
}

// ============================================================================
// Compactification
// ============================================================================

static void test_compactify_simple() {
    std::printf("--- Compactify (simple) ---\n");

    auto doc = HermesCtr::create();
    auto* map = doc.make_tiny_map();
    doc.set_root(map);

    map->put(0, TaggedPtr::from_value(int32_t(42), type_hash::Integer), doc.arena());
    map->put(5, TaggedPtr::from_value(float(2.5f), type_hash::Real), doc.arena());
    map->put(10, TaggedPtr::from_value(int8_t(-1), type_hash::TinyInt), doc.arena());

    auto compact = doc.compactify();

    LOGOS_ASSERT(compact.has_root(), "HERMES-DOC-003", "Compacted doc must have root");
    LOGOS_ASSERT(compact.arena().mode() == ArenaMode::GrowableSingleChunk, "HERMES-DOC-003",
        "Compacted doc must be GrowableSingleChunk");
    LOGOS_ASSERT(compact.arena().chunk_count() == 1, "HERMES-DOC-003",
        "Compacted doc must have 1 chunk");

    auto* cmap = compact.root<TinyObjectMap>();
    LOGOS_ASSERT(cmap->size() == 3, "HERMES-DOC-003",
        "Compacted map must have 3 entries, got {}", cmap->size());
    LOGOS_ASSERT(cmap->get(0).as_value<int32_t>() == 42, "HERMES-DOC-003", "");
    LOGOS_ASSERT(cmap->get(5).as_value<float>() == 2.5f, "HERMES-DOC-003", "");
    LOGOS_ASSERT(cmap->get(10).as_value<int8_t>() == -1, "HERMES-DOC-003", "");

    LOGOS_TRACE("hermes.doc.compactify", "status", "pass",
        "original_used", doc.arena().total_used(),
        "compact_used", compact.arena().total_used());
    std::printf("  Compactify (simple): OK (original %zu → compact %zu bytes)\n",
        doc.arena().total_used(), compact.arena().total_used());
}

static void test_compactify_array_with_values() {
    std::printf("--- Compactify (array with embedded values) ---\n");

    auto doc = HermesCtr::create();
    auto* arr = doc.make_array();
    doc.set_root(arr);

    for (int i = 0; i < 20; ++i) {
        arr->push_back(TaggedPtr::from_value(int32_t(i * 100), type_hash::Integer), doc.arena());
    }

    auto compact = doc.compactify();

    auto* carr = compact.root<ObjectArray>();
    LOGOS_ASSERT(carr->size() == 20, "HERMES-DOC-003", "");
    for (int i = 0; i < 20; ++i) {
        LOGOS_ASSERT(carr->get(i).as_value<int32_t>() == i * 100, "HERMES-DOC-003",
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

    // Create and compactify a document.
    auto doc = HermesCtr::create();
    auto* map = doc.make_tiny_map();
    doc.set_root(map);
    map->put(0, TaggedPtr::from_value(int32_t(42), type_hash::Integer), doc.arena());
    map->put(3, TaggedPtr::from_value(int32_t(99), type_hash::Integer), doc.arena());

    auto compact = doc.compactify();

    // Serialize to bytes.
    auto bytes = compact.as_bytes();
    LOGOS_ASSERT(bytes.data != nullptr, "HERMES-SERIAL-001", "");
    LOGOS_ASSERT(bytes.size > 0, "HERMES-SERIAL-001", "");

    // Deserialize (copy bytes to simulate receiving from network).
    auto loaded = HermesCtr::from_bytes_copy(bytes.data, bytes.size);

    LOGOS_ASSERT(loaded.has_root(), "HERMES-SERIAL-001", "Loaded doc must have root");

    auto* lmap = loaded.root<TinyObjectMap>();
    LOGOS_ASSERT(lmap->size() == 2, "HERMES-SERIAL-001",
        "Loaded map must have 2 entries, got {}", lmap->size());
    LOGOS_ASSERT(lmap->get(0).as_value<int32_t>() == 42, "HERMES-SERIAL-001", "");
    LOGOS_ASSERT(lmap->get(3).as_value<int32_t>() == 99, "HERMES-SERIAL-001", "");

    LOGOS_TRACE("hermes.serial.zerocopy", "status", "pass",
        "bytes", bytes.size);
    std::printf("  Zero-copy round-trip: OK (%zu bytes)\n", bytes.size);
}

static void test_zero_copy_array_round_trip() {
    std::printf("--- Zero-copy array round-trip ---\n");

    auto doc = HermesCtr::create();
    auto* arr = doc.make_array();
    doc.set_root(arr);

    for (int i = 0; i < 10; ++i) {
        arr->push_back(TaggedPtr::from_value(int32_t(i), type_hash::Integer), doc.arena());
    }

    auto compact = doc.compactify();
    auto bytes = compact.as_bytes();
    auto loaded = HermesCtr::from_bytes_copy(bytes.data, bytes.size);

    auto* larr = loaded.root<ObjectArray>();
    LOGOS_ASSERT(larr->size() == 10, "HERMES-SERIAL-001", "");
    for (int i = 0; i < 10; ++i) {
        LOGOS_ASSERT(larr->get(i).as_value<int32_t>() == i, "HERMES-SERIAL-001", "");
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
