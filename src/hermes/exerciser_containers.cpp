// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <bit>

using namespace logos::hermes;

// ============================================================================
// TinyObjectMap tests
// ============================================================================

static void test_tiny_map_basic() {
    std::printf("--- TinyObjectMap basic ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* map = TinyObjectMap::create(arena);

    LOGOS_ASSERT(map->size() == 0, "INV-TINYMAP-001", "New map must have size 0");
    LOGOS_ASSERT(!map->has_key(0), "INV-TINYMAP-001", "New map must have no keys");

    // Put some values.
    map->put(0, TaggedPtr::from_value(int32_t(42), type_hash::Integer), arena);
    map->put(5, TaggedPtr::from_value(int32_t(99), type_hash::Integer), arena);
    map->put(10, TaggedPtr::from_value(int8_t(-1), type_hash::TinyInt), arena);

    LOGOS_ASSERT(map->size() == 3, "INV-TINYMAP-001",
        "Map size must be 3, got {}", map->size());
    LOGOS_ASSERT(map->size() <= map->capacity(), "INV-TINYMAP-001",
        "size ({}) must be <= capacity ({})", map->size(), map->capacity());

    // Bitmap popcount must match size.
    LOGOS_ASSERT(std::popcount(map->bitmap()) == map->size(), "INV-TINYMAP-002",
        "PopCnt(bitmap) must equal size");

    // Get values back.
    LOGOS_ASSERT(map->has_key(0), "INV-TINYMAP-001", "Key 0 must be present");
    LOGOS_ASSERT(map->has_key(5), "INV-TINYMAP-001", "Key 5 must be present");
    LOGOS_ASSERT(map->has_key(10), "INV-TINYMAP-001", "Key 10 must be present");
    LOGOS_ASSERT(!map->has_key(1), "INV-TINYMAP-001", "Key 1 must be absent");

    TaggedPtr v0 = map->get(0, base);
    LOGOS_ASSERT(v0.is_value(), "INV-TINYMAP-001", "Value at key 0 must be embedded");
    LOGOS_ASSERT(v0.as_value<int32_t>() == 42, "INV-TINYMAP-001",
        "Value at key 0 must be 42, got {}", v0.as_value<int32_t>());

    TaggedPtr v5 = map->get(5, base);
    LOGOS_ASSERT(v5.as_value<int32_t>() == 99, "INV-TINYMAP-001", "");

    TaggedPtr v10 = map->get(10, base);
    LOGOS_ASSERT(v10.as_value<int8_t>() == -1, "INV-TINYMAP-001", "");

    // Missing key returns null.
    TaggedPtr vmiss = map->get(1, base);
    LOGOS_ASSERT(vmiss.is_null(), "INV-TINYMAP-001", "Missing key must return null");

    LOGOS_TRACE("hermes.tinymap.basic", "status", "pass");
    std::printf("  TinyObjectMap basic: OK\n");
}

static void test_tiny_map_update() {
    std::printf("--- TinyObjectMap update ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* map = TinyObjectMap::create(arena);

    map->put(3, TaggedPtr::from_value(int32_t(10), type_hash::Integer), arena);
    LOGOS_ASSERT(map->get(3, base).as_value<int32_t>() == 10, "INV-TINYMAP-001", "");

    // Update existing key.
    map->put(3, TaggedPtr::from_value(int32_t(20), type_hash::Integer), arena);
    LOGOS_ASSERT(map->size() == 1, "INV-TINYMAP-001",
        "Update must not increase size");
    LOGOS_ASSERT(map->get(3, base).as_value<int32_t>() == 20, "INV-TINYMAP-001",
        "Updated value must be 20");

    LOGOS_TRACE("hermes.tinymap.update", "status", "pass");
    std::printf("  TinyObjectMap update: OK\n");
}

static void test_tiny_map_remove() {
    std::printf("--- TinyObjectMap remove ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* map = TinyObjectMap::create(arena);

    map->put(0, TaggedPtr::from_value(int32_t(1), type_hash::Integer), arena);
    map->put(1, TaggedPtr::from_value(int32_t(2), type_hash::Integer), arena);
    map->put(2, TaggedPtr::from_value(int32_t(3), type_hash::Integer), arena);

    bool removed = map->remove(1, base);
    LOGOS_ASSERT(removed, "INV-TINYMAP-001", "Remove of existing key must return true");
    LOGOS_ASSERT(map->size() == 2, "INV-TINYMAP-001", "Size after remove must be 2");
    LOGOS_ASSERT(!map->has_key(1), "INV-TINYMAP-001", "Removed key must be absent");
    LOGOS_ASSERT(std::popcount(map->bitmap()) == map->size(), "INV-TINYMAP-002", "");

    // Remaining keys intact.
    LOGOS_ASSERT(map->get(0, base).as_value<int32_t>() == 1, "INV-TINYMAP-001", "");
    LOGOS_ASSERT(map->get(2, base).as_value<int32_t>() == 3, "INV-TINYMAP-001", "");

    // Remove non-existent.
    LOGOS_ASSERT(!map->remove(1, base), "INV-TINYMAP-001",
        "Remove of absent key must return false");

    LOGOS_TRACE("hermes.tinymap.remove", "status", "pass");
    std::printf("  TinyObjectMap remove: OK\n");
}

static void test_tiny_map_stress() {
    std::printf("--- TinyObjectMap stress (all 52 keys) ---\n");

    Arena arena(ArenaMode::MultiChunk, 8192);
    uint8_t* base = arena.head().data();
    auto* map = TinyObjectMap::create(arena, 0);

    // Fill all 52 keys.
    for (uint8_t k = 0; k < 52; ++k) {
        map->put(k, TaggedPtr::from_value(int32_t(k * 10), type_hash::Integer), arena);
    }

    LOGOS_ASSERT(map->size() == 52, "INV-TINYMAP-001",
        "Full map must have size 52, got {}", map->size());
    LOGOS_ASSERT(map->size() <= map->capacity(), "INV-TINYMAP-001", "");
    LOGOS_ASSERT(std::popcount(map->bitmap()) == 52, "INV-TINYMAP-002", "");

    // Verify all values.
    for (uint8_t k = 0; k < 52; ++k) {
        TaggedPtr v = map->get(k, base);
        LOGOS_ASSERT(v.as_value<int32_t>() == int32_t(k * 10), "INV-TINYMAP-001",
            "Value at key {} must be {}, got {}", k, k * 10, v.as_value<int32_t>());
    }

    // TypeTag check.
    TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(map));
    LOGOS_ASSERT(tag.type_code() == type_hash::Hermes, "INV-TINYMAP-001",
        "TinyObjectMap type_code must be {}", type_hash::Hermes);
    LOGOS_ASSERT(tag.descriptor() == TagDescriptor::Map, "INV-TINYMAP-001",
        "TinyObjectMap descriptor must be Map");

    LOGOS_TRACE("hermes.tinymap.stress", "status", "pass", "size", map->size());
    std::printf("  TinyObjectMap stress: OK (52 keys)\n");
}

// ============================================================================
// ObjectArray tests
// ============================================================================

static void test_object_array_basic() {
    std::printf("--- ObjectArray basic ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* arr = ObjectArray::create(arena);

    LOGOS_ASSERT(arr->size() == 0, "HERMES-ARRAY-001", "New array must be empty");
    LOGOS_ASSERT(arr->empty(), "HERMES-ARRAY-001", "");

    // Push elements.
    arr->push_back(TaggedPtr::from_value(int32_t(10), type_hash::Integer), arena);
    arr->push_back(TaggedPtr::from_value(int32_t(20), type_hash::Integer), arena);
    arr->push_back(TaggedPtr::from_value(float(3.14f), type_hash::Real), arena);

    LOGOS_ASSERT(arr->size() == 3, "HERMES-ARRAY-001",
        "Array size must be 3, got {}", arr->size());

    LOGOS_ASSERT(arr->get(0, base).as_value<int32_t>() == 10, "HERMES-ARRAY-001", "");
    LOGOS_ASSERT(arr->get(1, base).as_value<int32_t>() == 20, "HERMES-ARRAY-001", "");
    LOGOS_ASSERT(arr->get(2, base).as_value<float>() == 3.14f, "HERMES-ARRAY-001", "");

    // Out of bounds returns null.
    LOGOS_ASSERT(arr->get(100, base).is_null(), "HERMES-ARRAY-001", "");

    // TypeTag check.
    TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(arr));
    LOGOS_ASSERT(tag.type_code() == type_hash::ObjectArray, "HERMES-ARRAY-002", "");
    LOGOS_ASSERT(tag.descriptor() == TagDescriptor::Array, "HERMES-ARRAY-002", "");

    LOGOS_TRACE("hermes.array.basic", "status", "pass");
    std::printf("  ObjectArray basic: OK\n");
}

static void test_object_array_grow() {
    std::printf("--- ObjectArray grow ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* arr = ObjectArray::create(arena, 2); // Small initial capacity.

    for (int i = 0; i < 100; ++i) {
        arr->push_back(TaggedPtr::from_value(int32_t(i), type_hash::Integer), arena);
    }

    LOGOS_ASSERT(arr->size() == 100, "HERMES-ARRAY-001",
        "Array must have 100 elements after push");
    LOGOS_ASSERT(arr->capacity() >= 100, "HERMES-ARRAY-001",
        "Array capacity must be >= 100");

    // Verify all values.
    for (int i = 0; i < 100; ++i) {
        LOGOS_ASSERT(arr->get(i, base).as_value<int32_t>() == i, "HERMES-ARRAY-001",
            "Array[{}] must be {}", i, i);
    }

    LOGOS_TRACE("hermes.array.grow", "status", "pass", "capacity", arr->capacity());
    std::printf("  ObjectArray grow: OK (capacity %lu)\n",
        static_cast<unsigned long>(arr->capacity()));
}

static void test_object_array_set_and_pop() {
    std::printf("--- ObjectArray set/pop ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* arr = ObjectArray::create(arena);

    arr->push_back(TaggedPtr::from_value(int32_t(1), type_hash::Integer), arena);
    arr->push_back(TaggedPtr::from_value(int32_t(2), type_hash::Integer), arena);
    arr->push_back(TaggedPtr::from_value(int32_t(3), type_hash::Integer), arena);

    // Set.
    arr->set(1, TaggedPtr::from_value(int32_t(99), type_hash::Integer), base);
    LOGOS_ASSERT(arr->get(1, base).as_value<int32_t>() == 99, "HERMES-ARRAY-001", "");

    // Pop.
    arr->pop_back(base);
    LOGOS_ASSERT(arr->size() == 2, "HERMES-ARRAY-001", "Size after pop must be 2");
    LOGOS_ASSERT(arr->get(0, base).as_value<int32_t>() == 1, "HERMES-ARRAY-001", "");
    LOGOS_ASSERT(arr->get(1, base).as_value<int32_t>() == 99, "HERMES-ARRAY-001", "");

    LOGOS_TRACE("hermes.array.set_pop", "status", "pass");
    std::printf("  ObjectArray set/pop: OK\n");
}

static void test_object_array_with_pointers() {
    std::printf("--- ObjectArray with arena pointers ---\n");

    Arena arena(ArenaMode::MultiChunk, 4096);
    uint8_t* base = arena.head().data();
    auto* arr = ObjectArray::create(arena);

    // IMPORTANT: pointer-mode TaggedPtrs contain relative offsets from their own
    // address, so they must be written in-place via slot(), never via a stack copy.
    ArenaString* s1 = ArenaString::create(arena, "hello");
    ArenaString* s2 = ArenaString::create(arena, "world");

    // Push null placeholders, then set pointers in-place via slot().
    arr->push_back(TaggedPtr{}, arena);
    arr->push_back(TaggedPtr{}, arena);

    arr->slot(0, base)->set_pointer(s1, base);
    arr->slot(1, base)->set_pointer(s2, base);

    // Retrieve and verify — read via slot() to get the offset from the correct address.
    TaggedPtr* r1 = arr->slot(0, base);
    LOGOS_ASSERT(r1->is_pointer(), "HERMES-ARRAY-001", "Element 0 must be pointer mode");
    ArenaString* rs1 = r1->as_ptr<ArenaString>(base);
    LOGOS_ASSERT(*rs1 == "hello", "HERMES-ARRAY-001",
        "Element 0 must point to 'hello'");

    TaggedPtr* r2 = arr->slot(1, base);
    ArenaString* rs2 = r2->as_ptr<ArenaString>(base);
    LOGOS_ASSERT(*rs2 == "world", "HERMES-ARRAY-001", "");

    LOGOS_TRACE("hermes.array.pointers", "status", "pass");
    std::printf("  ObjectArray with pointers: OK\n");
}

// ============================================================================
// ObjectMap tests
// ============================================================================

static void test_object_map_basic() {
    std::printf("--- ObjectMap basic ---\n");

    Arena arena(ArenaMode::MultiChunk, 8192);
    uint8_t* base = arena.head().data();
    auto* map = ObjectMap::create(arena);

    LOGOS_ASSERT(map->size() == 0, "HERMES-MAP-001", "New map must be empty");

    map->put("name", TaggedPtr::from_value(int32_t(42), type_hash::Integer), arena);
    map->put("age", TaggedPtr::from_value(int32_t(30), type_hash::Integer), arena);

    LOGOS_ASSERT(map->size() == 2, "HERMES-MAP-001", "Map size must be 2");
    LOGOS_ASSERT(map->has("name", base), "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->has("age", base), "HERMES-MAP-001", "");
    LOGOS_ASSERT(!map->has("missing", base), "HERMES-MAP-001", "");

    LOGOS_ASSERT(map->get("name", base).as_value<int32_t>() == 42, "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->get("age", base).as_value<int32_t>() == 30, "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->get("missing", base).is_null(), "HERMES-MAP-001", "");

    // TypeTag check.
    TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(map));
    LOGOS_ASSERT(tag.type_code() == type_hash::ObjectMap, "HERMES-MAP-002", "");
    LOGOS_ASSERT(tag.descriptor() == TagDescriptor::Map, "HERMES-MAP-002", "");

    LOGOS_TRACE("hermes.map.basic", "status", "pass");
    std::printf("  ObjectMap basic: OK\n");
}

static void test_object_map_update() {
    std::printf("--- ObjectMap update ---\n");

    Arena arena(ArenaMode::MultiChunk, 8192);
    uint8_t* base = arena.head().data();
    auto* map = ObjectMap::create(arena);

    map->put("key", TaggedPtr::from_value(int32_t(1), type_hash::Integer), arena);
    map->put("key", TaggedPtr::from_value(int32_t(2), type_hash::Integer), arena);

    LOGOS_ASSERT(map->size() == 1, "HERMES-MAP-001", "Update must not increase size");
    LOGOS_ASSERT(map->get("key", base).as_value<int32_t>() == 2, "HERMES-MAP-001", "");

    LOGOS_TRACE("hermes.map.update", "status", "pass");
    std::printf("  ObjectMap update: OK\n");
}

static void test_object_map_remove() {
    std::printf("--- ObjectMap remove ---\n");

    Arena arena(ArenaMode::MultiChunk, 8192);
    uint8_t* base = arena.head().data();
    auto* map = ObjectMap::create(arena);

    map->put("a", TaggedPtr::from_value(int32_t(1), type_hash::Integer), arena);
    map->put("b", TaggedPtr::from_value(int32_t(2), type_hash::Integer), arena);
    map->put("c", TaggedPtr::from_value(int32_t(3), type_hash::Integer), arena);

    LOGOS_ASSERT(map->remove("b", arena), "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->size() == 2, "HERMES-MAP-001", "");
    LOGOS_ASSERT(!map->has("b", base), "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->has("a", base), "HERMES-MAP-001", "");
    LOGOS_ASSERT(map->has("c", base), "HERMES-MAP-001", "");

    LOGOS_ASSERT(!map->remove("b", arena), "HERMES-MAP-001",
        "Double remove must return false");

    LOGOS_TRACE("hermes.map.remove", "status", "pass");
    std::printf("  ObjectMap remove: OK\n");
}

static void test_object_map_stress() {
    std::printf("--- ObjectMap stress (200 keys) ---\n");

    Arena arena(ArenaMode::MultiChunk, 65536);
    uint8_t* base = arena.head().data();
    auto* map = ObjectMap::create(arena);

    // Insert 200 unique keys (triggers rehash).
    char keybuf[32];
    for (int i = 0; i < 200; ++i) {
        std::snprintf(keybuf, sizeof(keybuf), "key_%03d", i);
        map->put(keybuf, TaggedPtr::from_value(int32_t(i), type_hash::Integer), arena);
    }

    LOGOS_ASSERT(map->size() == 200, "HERMES-MAP-001",
        "Map must have 200 entries, got {}", map->size());

    // Verify all values.
    for (int i = 0; i < 200; ++i) {
        std::snprintf(keybuf, sizeof(keybuf), "key_%03d", i);
        TaggedPtr v = map->get(keybuf, base);
        LOGOS_ASSERT(!v.is_null(), "HERMES-MAP-001",
            "Key '{}' must be present", keybuf);
        LOGOS_ASSERT(v.as_value<int32_t>() == i, "HERMES-MAP-001",
            "Value for '{}' must be {}, got {}", keybuf, i, v.as_value<int32_t>());
    }

    LOGOS_TRACE("hermes.map.stress", "status", "pass",
        "size", map->size(), "buckets", map->bucket_count());
    std::printf("  ObjectMap stress: OK (200 keys, %lu buckets)\n",
        static_cast<unsigned long>(map->bucket_count()));
}

static void test_object_map_with_string_values() {
    std::printf("--- ObjectMap with string values ---\n");

    Arena arena(ArenaMode::MultiChunk, 8192);
    uint8_t* base = arena.head().data();
    auto* map = ObjectMap::create(arena);

    ArenaString* greeting = ArenaString::create(arena, "Hello, World!");

    // First put a null placeholder, then set the pointer in-place via get_slot.
    map->put("greeting", TaggedPtr{}, arena);
    TaggedPtr* slot = map->get_slot("greeting", base);
    LOGOS_ASSERT(slot != nullptr, "HERMES-MAP-001", "Slot must exist after put");
    slot->set_pointer(greeting, base);

    // Retrieve via get_slot (returns pointer to the slot, preserving relative offset).
    TaggedPtr* result = map->get_slot("greeting", base);
    LOGOS_ASSERT(result != nullptr, "HERMES-MAP-001", "");
    LOGOS_ASSERT(result->is_pointer(), "HERMES-MAP-001", "");
    ArenaString* s = result->as_ptr<ArenaString>(base);
    LOGOS_ASSERT(*s == "Hello, World!", "HERMES-MAP-001", "");

    LOGOS_TRACE("hermes.map.string_values", "status", "pass");
    std::printf("  ObjectMap with string values: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes Layer 2: Containers Exerciser ===\n\n");

    test_tiny_map_basic();
    test_tiny_map_update();
    test_tiny_map_remove();
    test_tiny_map_stress();

    test_object_array_basic();
    test_object_array_grow();
    test_object_array_set_and_pop();
    test_object_array_with_pointers();

    test_object_map_basic();
    test_object_map_update();
    test_object_map_remove();
    test_object_map_stress();
    test_object_map_with_string_values();

    std::printf("\n=== All Layer 2 tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
