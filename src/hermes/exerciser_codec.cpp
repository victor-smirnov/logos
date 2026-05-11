// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/access.hpp>
#include <logos/hermes/arena_value.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/clone.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>

using namespace logos::hermes;

// ============================================================================
// Deep copy with pointer-mode values
// ============================================================================

static void test_deep_copy_tiny_map_with_pointers() {
    std::printf("--- Deep copy TinyObjectMap with pointers ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);

    // Key 0 = embedded int
    map.put(0, AnyVal::from_value(int32_t(42))).get();

    // Key 1 = pointer to string
    auto s = doc.make_string("hello from pointer").get();
    map.put(1, AnyVal{}).get();
    map.slot(1)->set_pointer(s.ptr(), HermesAccess::base(doc));

    // Key 2 = pointer-mode float (floats no longer embed in the 4-byte AnyVal)
    map.put(2, anyval_put<float>(HermesAccess::arena(doc), 2.5f).get()).get();

    // Compactify (deep copy).
    auto compact = clone(doc).get();
    uint8_t* cb = HermesAccess::base(compact);
    auto* cmap = HermesAccess::root<TinyObjectMap>(compact);

    LOGOS_ASSERT(cmap->size() == 3, "HERMES-DEEPCOPY-001", "");
    LOGOS_ASSERT(cmap->get(0, cb).as_value<int32_t>() == 42, "HERMES-DEEPCOPY-001", "");
    LOGOS_ASSERT(*cmap->get(2, cb).as_ptr<float>(cb) == 2.5f, "HERMES-DEEPCOPY-001", "");

    // Check the pointer-mode value.
    AnyVal* slot1 = cmap->slot(1, cb);
    LOGOS_ASSERT(slot1->is_pointer(), "HERMES-DEEPCOPY-001",
        "Key 1 must still be pointer mode after deep copy");
    auto* cs = slot1->as_ptr<ArenaString>(cb);
    LOGOS_ASSERT(*cs == "hello from pointer", "HERMES-DEEPCOPY-001",
        "Deep-copied string must match original");

    LOGOS_TRACE("hermes.deepcopy.tinymap_ptrs", "status", "pass");
    std::printf("  Deep copy TinyObjectMap with pointers: OK\n");
}

static void test_deep_copy_object_map() {
    std::printf("--- Deep copy ObjectMap ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_object_map().get();
    doc.set_root(map);

    map.put("name", AnyVal::from_value(int32_t(1))).get();
    map.put("count", AnyVal::from_value(int32_t(2))).get();

    // Add a pointer-mode value.
    auto s = doc.make_string("value_string").get();
    map.put("text", AnyVal{}).get();
    map.get_slot("text")->set_pointer(s.ptr(), HermesAccess::base(doc));

    auto compact = clone(doc).get();
    uint8_t* cb = HermesAccess::base(compact);
    auto* cmap = HermesAccess::root<ObjectMap>(compact);

    LOGOS_ASSERT(cmap->size() == 3, "HERMES-DEEPCOPY-002",
        "Compacted ObjectMap must have 3 entries, got {}", cmap->size());
    LOGOS_ASSERT(cmap->get("name", cb).as_value<int32_t>() == 1, "HERMES-DEEPCOPY-002", "");
    LOGOS_ASSERT(cmap->get("count", cb).as_value<int32_t>() == 2, "HERMES-DEEPCOPY-002", "");

    AnyVal* text_slot = cmap->get_slot("text", cb);
    LOGOS_ASSERT(text_slot != nullptr, "HERMES-DEEPCOPY-002", "");
    LOGOS_ASSERT(text_slot->is_pointer(), "HERMES-DEEPCOPY-002", "");
    LOGOS_ASSERT(*text_slot->as_ptr<ArenaString>(cb) == "value_string", "HERMES-DEEPCOPY-002", "");

    LOGOS_TRACE("hermes.deepcopy.objmap", "status", "pass");
    std::printf("  Deep copy ObjectMap: OK\n");
}

// ============================================================================
// Binary codec: TinyObjectMap
// ============================================================================

static void test_binary_tiny_map() {
    std::printf("--- Binary codec TinyObjectMap ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);

    map.put(0, AnyVal::from_value(int32_t(42))).get();
    map.put(5, anyval_put<float>(HermesAccess::arena(doc), 3.14f).get()).get();
    map.put(10, AnyVal::from_value(int8_t(-1))).get();

    auto bytes = binary_encode(doc).get();
    LOGOS_ASSERT(!bytes.empty(), "HERMES-BINARY-001", "Encoded bytes must not be empty");

    auto decoded = binary_decode(bytes.data(), bytes.size()).get();
    uint8_t* db = HermesAccess::base(decoded);
    auto* dmap = HermesAccess::root<TinyObjectMap>(decoded);

    LOGOS_ASSERT(dmap->size() == 3, "HERMES-BINARY-003",
        "Decoded map must have 3 entries, got {}", dmap->size());
    LOGOS_ASSERT(dmap->get(0, db).as_value<int32_t>() == 42, "HERMES-BINARY-003", "");
    LOGOS_ASSERT(*dmap->get(5, db).as_ptr<float>(db) == 3.14f, "HERMES-BINARY-003", "");
    LOGOS_ASSERT(dmap->get(10, db).as_value<int8_t>() == -1, "HERMES-BINARY-003", "");

    LOGOS_TRACE("hermes.binary.tinymap", "status", "pass", "bytes", bytes.size());
    std::printf("  Binary TinyObjectMap: OK (%zu bytes)\n", bytes.size());
}

// ============================================================================
// Binary codec: ObjectArray
// ============================================================================

static void test_binary_object_array() {
    std::printf("--- Binary codec ObjectArray ---\n");

    auto doc = make_doc().get();
    auto arr = doc.make_array().get();
    doc.set_root(arr);

    for (int i = 0; i < 10; ++i) {
        arr.push_back(AnyVal::from_value(int32_t(i * 100))).get();
    }

    auto bytes = binary_encode(doc).get();
    auto decoded = binary_decode(bytes.data(), bytes.size()).get();
    uint8_t* db = HermesAccess::base(decoded);

    auto* darr = HermesAccess::root<ObjectArray>(decoded);
    LOGOS_ASSERT(darr->size() == 10, "HERMES-BINARY-003", "");
    for (int i = 0; i < 10; ++i) {
        LOGOS_ASSERT(darr->get(i, db).as_value<int32_t>() == i * 100, "HERMES-BINARY-003",
            "Decoded array[{}] must be {}", i, i * 100);
    }

    LOGOS_TRACE("hermes.binary.array", "status", "pass", "bytes", bytes.size());
    std::printf("  Binary ObjectArray: OK (%zu bytes)\n", bytes.size());
}

// ============================================================================
// Binary codec: ObjectMap
// ============================================================================

static void test_binary_object_map() {
    std::printf("--- Binary codec ObjectMap ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_object_map().get();
    doc.set_root(map);

    map.put("name", AnyVal::from_value(int32_t(42))).get();
    map.put("value", AnyVal::from_value(int32_t(99))).get();

    auto bytes = binary_encode(doc).get();
    auto decoded = binary_decode(bytes.data(), bytes.size()).get();
    uint8_t* db = HermesAccess::base(decoded);

    auto* dmap = HermesAccess::root<ObjectMap>(decoded);
    LOGOS_ASSERT(dmap->size() == 2, "HERMES-BINARY-003",
        "Decoded map must have 2 entries, got {}", dmap->size());
    LOGOS_ASSERT(dmap->get("name", db).as_value<int32_t>() == 42, "HERMES-BINARY-003", "");
    LOGOS_ASSERT(dmap->get("value", db).as_value<int32_t>() == 99, "HERMES-BINARY-003", "");

    LOGOS_TRACE("hermes.binary.objmap", "status", "pass", "bytes", bytes.size());
    std::printf("  Binary ObjectMap: OK (%zu bytes)\n", bytes.size());
}

// ============================================================================
// Binary codec: nested structure
// ============================================================================

static void test_binary_nested() {
    std::printf("--- Binary codec nested structure ---\n");

    auto doc = make_doc().get();
    auto root = doc.make_tiny_map().get();
    doc.set_root(root);

    // Key 0 = embedded int
    root.put(0, AnyVal::from_value(int32_t(1))).get();

    // Key 1 = pointer to string
    auto s = doc.make_string("nested_string").get();
    root.put(1, AnyVal{}).get();
    root.slot(1)->set_pointer(s.ptr(), HermesAccess::base(doc));

    // Key 2 = pointer to an array
    auto inner_arr = doc.make_array().get();
    inner_arr.push_back(AnyVal::from_value(int32_t(10))).get();
    inner_arr.push_back(AnyVal::from_value(int32_t(20))).get();
    root.put(2, AnyVal{}).get();
    root.slot(2)->set_pointer(inner_arr.ptr(), HermesAccess::base(doc));

    auto bytes = binary_encode(doc).get();
    auto decoded = binary_decode(bytes.data(), bytes.size()).get();
    uint8_t* db = HermesAccess::base(decoded);

    auto* droot = HermesAccess::root<TinyObjectMap>(decoded);
    LOGOS_ASSERT(droot->size() == 3, "HERMES-BINARY-004", "");
    LOGOS_ASSERT(droot->get(0, db).as_value<int32_t>() == 1, "HERMES-BINARY-004", "");

    // String
    AnyVal* str_slot = droot->slot(1, db);
    LOGOS_ASSERT(str_slot->is_pointer(), "HERMES-BINARY-004", "");
    LOGOS_ASSERT(*str_slot->as_ptr<ArenaString>(db) == "nested_string", "HERMES-BINARY-004", "");

    // Array
    AnyVal* arr_slot = droot->slot(2, db);
    LOGOS_ASSERT(arr_slot->is_pointer(), "HERMES-BINARY-004", "");
    auto* darr = arr_slot->as_ptr<ObjectArray>(db);
    LOGOS_ASSERT(darr->size() == 2, "HERMES-BINARY-004", "");
    LOGOS_ASSERT(darr->get(0, db).as_value<int32_t>() == 10, "HERMES-BINARY-004", "");
    LOGOS_ASSERT(darr->get(1, db).as_value<int32_t>() == 20, "HERMES-BINARY-004", "");

    LOGOS_TRACE("hermes.binary.nested", "status", "pass", "bytes", bytes.size());
    std::printf("  Binary nested: OK (%zu bytes)\n", bytes.size());
}

// ============================================================================
// Binary codec: double round-trip
// ============================================================================

static void test_binary_double_round_trip() {
    std::printf("--- Binary double round-trip ---\n");

    auto doc = make_doc().get();
    auto map = doc.make_tiny_map().get();
    doc.set_root(map);

    map.put(0, AnyVal::from_value(int32_t(42))).get();
    map.put(1, anyval_put<float>(HermesAccess::arena(doc), 3.14f).get()).get();

    // Encode → decode → encode → compare.
    auto bytes1 = binary_encode(doc).get();
    auto doc2 = binary_decode(bytes1.data(), bytes1.size()).get();
    auto bytes2 = binary_encode(doc2).get();

    LOGOS_ASSERT(bytes1.size() == bytes2.size(), "HERMES-BINARY-005",
        "Double round-trip must produce same size: {} vs {}", bytes1.size(), bytes2.size());
    LOGOS_ASSERT(bytes1 == bytes2, "HERMES-BINARY-005",
        "Double round-trip must produce identical bytes");

    LOGOS_TRACE("hermes.binary.double_rt", "status", "pass");
    std::printf("  Binary double round-trip: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes: Deep Copy & Binary Codec Exerciser ===\n\n");

    test_deep_copy_tiny_map_with_pointers();
    test_deep_copy_object_map();

    test_binary_tiny_map();
    test_binary_object_array();
    test_binary_object_map();
    test_binary_nested();
    test_binary_double_round_trip();

    std::printf("\n=== All tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
