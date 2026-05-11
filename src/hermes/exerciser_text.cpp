// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/access.hpp>
#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/object_map.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/compound_types.hpp>
#include <logos/verification/assert.hpp>
#include <logos/verification/trace.hpp>
#include <logos/verification/sqlite_sink.hpp>

#include <cstdio>
#include <cstring>
#include <cmath>

using namespace logos::hermes;

// Exerciser helper — unwrap parse_doc() result, terminate on error.
static Hermes parse_doc(std::string_view text) {
    return logos::hermes::parse(text).get();
}

// ============================================================================
// Parse simple values
// ============================================================================

static void test_parse_integers() {
    std::printf("--- Parse integers ---\n");

    {
        auto doc = parse_doc("42");
        auto* root = HermesAccess::root<int32_t>(doc);
        LOGOS_ASSERT(*root == 42, "HERMES-PARSE-001", "Expected 42, got {}", *root);
    }
    {
        auto doc = parse_doc("-7");
        auto* root = HermesAccess::root<int32_t>(doc);
        LOGOS_ASSERT(*root == -7, "HERMES-PARSE-001", "Expected -7, got {}", *root);
    }
    {
        auto doc = parse_doc("100ll");
        auto* root = HermesAccess::root<int64_t>(doc);
        LOGOS_ASSERT(*root == 100, "HERMES-PARSE-001", "");
    }
    {
        auto doc = parse_doc("255_u8");
        auto* root = HermesAccess::root<uint8_t>(doc);
        LOGOS_ASSERT(*root == 255, "HERMES-PARSE-001", "");
    }
    {
        auto doc = parse_doc("0xFF_u32");
        auto* root = HermesAccess::root<uint32_t>(doc);
        LOGOS_ASSERT(*root == 255, "HERMES-PARSE-001", "");
    }
    {
        auto doc = parse_doc("0b1010_u16");
        auto* root = HermesAccess::root<uint16_t>(doc);
        LOGOS_ASSERT(*root == 10, "HERMES-PARSE-001", "");
    }

    LOGOS_TRACE("hermes.parse.integers", "status", "pass");
    std::printf("  Parse integers: OK\n");
}

static void test_parse_floats() {
    std::printf("--- Parse floats ---\n");

    {
        auto doc = parse_doc("3.14");
        auto* root = HermesAccess::root<float>(doc);
        LOGOS_ASSERT(std::abs(*root - 3.14f) < 0.001f, "HERMES-PARSE-002", "");
    }
    {
        auto doc = parse_doc("3.14f");
        auto* root = HermesAccess::root<float>(doc);
        LOGOS_ASSERT(std::abs(*root - 3.14f) < 0.001f, "HERMES-PARSE-002", "");
    }
    {
        auto doc = parse_doc("2.718d");
        auto* root = HermesAccess::root<double>(doc);
        LOGOS_ASSERT(std::abs(*root - 2.718) < 0.001, "HERMES-PARSE-002", "");
    }
    {
        auto doc = parse_doc("1e3");
        auto* root = HermesAccess::root<float>(doc);
        LOGOS_ASSERT(std::abs(*root - 1000.0f) < 0.1f, "HERMES-PARSE-002", "");
    }

    LOGOS_TRACE("hermes.parse.floats", "status", "pass");
    std::printf("  Parse floats: OK\n");
}

static void test_parse_booleans_null() {
    std::printf("--- Parse booleans & null ---\n");

    {
        auto doc = parse_doc("true");
        auto* root = HermesAccess::root<uint8_t>(doc);
        LOGOS_ASSERT(*root == 1, "HERMES-PARSE-003", "true must be 1");
    }
    {
        auto doc = parse_doc("false");
        auto* root = HermesAccess::root<uint8_t>(doc);
        LOGOS_ASSERT(*root == 0, "HERMES-PARSE-003", "false must be 0");
    }
    {
        auto doc = parse_doc("null");
        LOGOS_ASSERT(doc.has_root(), "HERMES-PARSE-003", "");
    }

    LOGOS_TRACE("hermes.parse.bool_null", "status", "pass");
    std::printf("  Parse booleans & null: OK\n");
}

static void test_parse_strings() {
    std::printf("--- Parse strings ---\n");

    {
        auto doc = parse_doc("\"hello\"");
        auto* root = HermesAccess::root<ArenaString>(doc);
        LOGOS_ASSERT(*root == "hello", "HERMES-PARSE-004", "");
    }
    {
        auto doc = parse_doc("\"line1\\nline2\"");
        auto* root = HermesAccess::root<ArenaString>(doc);
        LOGOS_ASSERT(*root == "line1\nline2", "HERMES-PARSE-004", "");
    }
    {
        auto doc = parse_doc("'raw string'");
        auto* root = HermesAccess::root<ArenaString>(doc);
        LOGOS_ASSERT(*root == "raw string", "HERMES-PARSE-004", "");
    }
    {
        auto doc = parse_doc("'can\\'t'");
        auto* root = HermesAccess::root<ArenaString>(doc);
        LOGOS_ASSERT(*root == "can't", "HERMES-PARSE-004", "");
    }
    {
        auto doc = parse_doc("\"\\u0041\""); // 'A'
        auto* root = HermesAccess::root<ArenaString>(doc);
        LOGOS_ASSERT(*root == "A", "HERMES-PARSE-004", "Unicode escape failed");
    }

    LOGOS_TRACE("hermes.parse.strings", "status", "pass");
    std::printf("  Parse strings: OK\n");
}

// ============================================================================
// Parse containers
// ============================================================================

static void test_parse_array() {
    std::printf("--- Parse array ---\n");

    {
        auto doc = parse_doc("[1, 2, 3]");
        uint8_t* base = HermesAccess::base(doc);
        auto* arr = HermesAccess::root<ObjectArray>(doc);
        LOGOS_ASSERT(arr->size() == 3, "HERMES-PARSE-005",
            "Array size must be 3, got {}", arr->size());
        LOGOS_ASSERT(arr->get(0, base).as_value<int32_t>() == 1, "HERMES-PARSE-005", "");
        LOGOS_ASSERT(arr->get(1, base).as_value<int32_t>() == 2, "HERMES-PARSE-005", "");
        LOGOS_ASSERT(arr->get(2, base).as_value<int32_t>() == 3, "HERMES-PARSE-005", "");
    }
    {
        auto doc = parse_doc("[]");
        auto* arr = HermesAccess::root<ObjectArray>(doc);
        LOGOS_ASSERT(arr->size() == 0, "HERMES-PARSE-005", "");
    }
    {
        // Mixed types.
        auto doc = parse_doc("[1, 3.14, \"hello\"]");
        uint8_t* base = HermesAccess::base(doc);
        auto* arr = HermesAccess::root<ObjectArray>(doc);
        LOGOS_ASSERT(arr->size() == 3, "HERMES-PARSE-005", "");
        LOGOS_ASSERT(arr->get(0, base).as_value<int32_t>() == 1, "HERMES-PARSE-005", "");
        // Float pointer-mode.
        LOGOS_ASSERT(std::abs(*arr->get(1, base).as_ptr<float>(base) - 3.14f) < 0.01f, "HERMES-PARSE-005", "");
        // String is pointer-mode.
        AnyVal* slot2 = arr->slot(2, base);
        LOGOS_ASSERT(slot2->is_pointer(), "HERMES-PARSE-005", "");
        LOGOS_ASSERT(*slot2->as_ptr<ArenaString>(base) == "hello", "HERMES-PARSE-005", "");
    }

    LOGOS_TRACE("hermes.parse.array", "status", "pass");
    std::printf("  Parse array: OK\n");
}

static void test_parse_map() {
    std::printf("--- Parse map ---\n");

    {
        auto doc = parse_doc("{name: \"Alice\", age: 30}");
        uint8_t* base = HermesAccess::base(doc);
        auto* map = HermesAccess::root<ObjectMap>(doc);
        LOGOS_ASSERT(map->size() == 2, "HERMES-PARSE-006",
            "Map size must be 2, got {}", map->size());
        LOGOS_ASSERT(map->get("age", base).as_value<int32_t>() == 30, "HERMES-PARSE-006", "");

        AnyVal* name_slot = map->get_slot("name", base);
        LOGOS_ASSERT(name_slot != nullptr, "HERMES-PARSE-006", "");
        LOGOS_ASSERT(name_slot->is_pointer(), "HERMES-PARSE-006", "");
        LOGOS_ASSERT(*name_slot->as_ptr<ArenaString>(base) == "Alice", "HERMES-PARSE-006", "");
    }
    {
        auto doc = parse_doc("{}");
        auto* map = HermesAccess::root<ObjectMap>(doc);
        LOGOS_ASSERT(map->size() == 0, "HERMES-PARSE-006", "");
    }
    {
        // Quoted keys.
        auto doc = parse_doc("{\"key 1\": 1, \"key 2\": 2}");
        uint8_t* base = HermesAccess::base(doc);
        auto* map = HermesAccess::root<ObjectMap>(doc);
        LOGOS_ASSERT(map->size() == 2, "HERMES-PARSE-006", "");
        LOGOS_ASSERT(map->get("key 1", base).as_value<int32_t>() == 1, "HERMES-PARSE-006", "");
    }

    LOGOS_TRACE("hermes.parse.map", "status", "pass");
    std::printf("  Parse map: OK\n");
}

static void test_parse_nested() {
    std::printf("--- Parse nested structure ---\n");

    auto doc = parse_doc(R"({
        user: {name: "Bob", id: 42},
        items: [1, 2, 3],
        active: true,
        rating: 4.5
    })");

    uint8_t* base = HermesAccess::base(doc);
    auto* root = HermesAccess::root<ObjectMap>(doc);
    LOGOS_ASSERT(root->size() == 4, "HERMES-PARSE-007",
        "Root map must have 4 entries, got {}", root->size());

    // active = true (embedded boolean)
    AnyVal active_val = root->get("active", base);
    LOGOS_ASSERT(active_val.as_value<uint8_t>() == 1, "HERMES-PARSE-007", "");

    // rating = 4.5f (pointer-mode float)
    LOGOS_ASSERT(std::abs(*root->get("rating", base).as_ptr<float>(base) - 4.5f) < 0.01f, "HERMES-PARSE-007", "");

    // items = [1,2,3] (pointer to array)
    AnyVal* items_slot = root->get_slot("items", base);
    LOGOS_ASSERT(items_slot->is_pointer(), "HERMES-PARSE-007", "");
    auto* items = items_slot->as_ptr<ObjectArray>(base);
    LOGOS_ASSERT(items->size() == 3, "HERMES-PARSE-007", "");

    // user = {name: "Bob", id: 42} (pointer to map)
    AnyVal* user_slot = root->get_slot("user", base);
    LOGOS_ASSERT(user_slot->is_pointer(), "HERMES-PARSE-007", "");
    auto* user = user_slot->as_ptr<ObjectMap>(base);
    LOGOS_ASSERT(user->get("id", base).as_value<int32_t>() == 42, "HERMES-PARSE-007", "");

    LOGOS_TRACE("hermes.parse.nested", "status", "pass");
    std::printf("  Parse nested: OK\n");
}

// ============================================================================
// Comments
// ============================================================================

static void test_parse_comments() {
    std::printf("--- Parse comments ---\n");

    auto doc = parse_doc(R"(
        // This is a comment
        [
            1, // inline comment
            2,
            3
        ]
    )");

    auto* arr = HermesAccess::root<ObjectArray>(doc);
    LOGOS_ASSERT(arr->size() == 3, "HERMES-PARSE-008", "");

    LOGOS_TRACE("hermes.parse.comments", "status", "pass");
    std::printf("  Parse comments: OK\n");
}

// ============================================================================
// Stringify
// ============================================================================

static void test_stringify_simple() {
    std::printf("--- Stringify simple ---\n");

    {
        auto doc = parse_doc("42");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "42", "HERMES-STR-001", "Expected '42', got '{}'", s);
    }
    {
        auto doc = parse_doc("\"hello\"");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "\"hello\"", "HERMES-STR-001", "Expected '\"hello\"', got '{}'", s);
    }
    {
        auto doc = parse_doc("true");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "true", "HERMES-STR-001", "Expected 'true', got '{}'", s);
    }

    LOGOS_TRACE("hermes.stringify.simple", "status", "pass");
    std::printf("  Stringify simple: OK\n");
}

static void test_stringify_containers() {
    std::printf("--- Stringify containers ---\n");

    {
        auto doc = parse_doc("[1, 2, 3]");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "[1,2,3]", "HERMES-STR-002",
            "Expected '[1,2,3]', got '{}'", s);
    }

    LOGOS_TRACE("hermes.stringify.containers", "status", "pass");
    std::printf("  Stringify containers: OK\n");
}

// ============================================================================
// Type declarations
// ============================================================================

static void test_parse_type_declarations() {
    std::printf("--- Parse type declarations ---\n");

    // Simple type name
    {
        auto doc = parse_doc("Integer");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "Integer", "HERMES-PARSE-TYPE-001",
            "Expected 'Integer', got '{}'", dt->name_view(base));
        LOGOS_ASSERT(!dt->has_params(), "HERMES-PARSE-TYPE-001", "No params expected");
    }

    // Parameterized type
    {
        auto doc = parse_doc("Array<Integer>");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "Array", "HERMES-PARSE-TYPE-002", "");
        LOGOS_ASSERT(dt->has_params(), "HERMES-PARSE-TYPE-002", "Must have params");
        LOGOS_ASSERT(dt->params.get(base)->size() == 1, "HERMES-PARSE-TYPE-002", "");
    }

    // Multi-param type
    {
        auto doc = parse_doc("Map<Varchar, Integer>");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "Map", "HERMES-PARSE-TYPE-003", "");
        LOGOS_ASSERT(dt->params.get(base)->size() == 2, "HERMES-PARSE-TYPE-003", "");
    }

    // Constructor args
    {
        auto doc = parse_doc("Decimal(10, 2)");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "Decimal", "HERMES-PARSE-TYPE-004", "");
        LOGOS_ASSERT(dt->has_ctr(), "HERMES-PARSE-TYPE-004", "Must have constructor args");
        LOGOS_ASSERT(dt->ctr.get(base)->size() == 2, "HERMES-PARSE-TYPE-004", "");
    }

    // Qualified name
    {
        auto doc = parse_doc("std::vector<int>");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "std::vector", "HERMES-PARSE-TYPE-005", "");
        LOGOS_ASSERT(dt->has_params(), "HERMES-PARSE-TYPE-005", "");
    }

    // C++ basic type
    {
        auto doc = parse_doc("unsigned long long");
        uint8_t* base = HermesAccess::base(doc);
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->name_view(base) == "unsigned long long", "HERMES-PARSE-TYPE-006",
            "Expected 'unsigned long long', got '{}'", dt->name_view(base));
    }

    LOGOS_TRACE("hermes.parse.types", "status", "pass");
    std::printf("  Parse type declarations: OK\n");
}

// ============================================================================
// Typed values
// ============================================================================

static void test_parse_typed_values() {
    std::printf("--- Parse typed values ---\n");

    {
        auto doc = parse_doc("@Integer = 42");
        uint8_t* base = HermesAccess::base(doc);
        auto* tv = HermesAccess::root<TypedValueData>(doc);

        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(tv));
        LOGOS_ASSERT(tag.type_code() == type_hash::TypedValue, "HERMES-PARSE-TV-001",
            "Root must be TypedValue");

        LOGOS_ASSERT(tv->datatype.get(base)->name_view(base) == "Integer", "HERMES-PARSE-TV-001", "");
        LOGOS_ASSERT(tv->value.is_value(), "HERMES-PARSE-TV-001", "Value must be embedded");
        LOGOS_ASSERT(tv->value.as_value<int32_t>() == 42, "HERMES-PARSE-TV-001", "");
    }

    {
        auto doc = parse_doc("@Decimal(50, 3) = \"19345\"");
        uint8_t* base = HermesAccess::base(doc);
        auto* tv = HermesAccess::root<TypedValueData>(doc);
        LOGOS_ASSERT(tv->datatype.get(base)->name_view(base) == "Decimal", "HERMES-PARSE-TV-002", "");
        LOGOS_ASSERT(tv->datatype.get(base)->has_ctr(), "HERMES-PARSE-TV-002", "");
        LOGOS_ASSERT(tv->value.is_pointer(), "HERMES-PARSE-TV-002", "String must be pointer");
        LOGOS_ASSERT(*tv->value.as_ptr<ArenaString>(base) == "19345", "HERMES-PARSE-TV-002", "");
    }

    LOGOS_TRACE("hermes.parse.typed_values", "status", "pass");
    std::printf("  Parse typed values: OK\n");
}

// ============================================================================
// Parameters
// ============================================================================

static void test_parse_parameters() {
    std::printf("--- Parse parameters ---\n");

    {
        auto doc = parse_doc("?userId");
        uint8_t* base = HermesAccess::base(doc);
        auto* p = HermesAccess::root<ParameterData>(doc);

        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(p));
        LOGOS_ASSERT(tag.type_code() == type_hash::Parameter, "HERMES-PARSE-PARAM-001",
            "Root must be Parameter");
        LOGOS_ASSERT(p->name_view(base) == "userId", "HERMES-PARSE-PARAM-001",
            "Expected 'userId', got '{}'", p->name_view(base));
    }

    LOGOS_TRACE("hermes.parse.parameters", "status", "pass");
    std::printf("  Parse parameters: OK\n");
}

// ============================================================================
// Stringify compound types
// ============================================================================

static void test_stringify_compound() {
    std::printf("--- Stringify compound types ---\n");

    {
        auto doc = parse_doc("Array<Integer>");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "Array<Integer>", "HERMES-STR-COMPOUND-001",
            "Expected 'Array<Integer>', got '{}'", s);
    }
    {
        auto doc = parse_doc("Decimal(10, 2)");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "Decimal(10, 2)", "HERMES-STR-COMPOUND-002",
            "Expected 'Decimal(10, 2)', got '{}'", s);
    }
    {
        auto doc = parse_doc("@Integer = 42");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "@Integer = 42", "HERMES-STR-COMPOUND-003",
            "Expected '@Integer = 42', got '{}'", s);
    }
    {
        auto doc = parse_doc("?myParam");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "?myParam", "HERMES-STR-COMPOUND-004",
            "Expected '?myParam', got '{}'", s);
    }

    LOGOS_TRACE("hermes.stringify.compound", "status", "pass");
    std::printf("  Stringify compound: OK\n");
}

// ============================================================================
// Typed containers
// ============================================================================

static void test_parse_typed_containers() {
    std::printf("--- Parse typed containers ---\n");

    {
        auto doc = parse_doc("<Integer>[1, 2, 3]");
        uint8_t* base = HermesAccess::base(doc);
        auto* tv = HermesAccess::root<TypedValueData>(doc);
        TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(tv));
        LOGOS_ASSERT(tag.type_code() == type_hash::TypedValue, "HERMES-PARSE-TC-001",
            "Typed container must produce TypedValue");
        auto* dt = tv->datatype.get(base);
        LOGOS_ASSERT(dt->name_view(base) == "Array", "HERMES-PARSE-TC-001",
            "Expected 'Array', got '{}'", dt->name_view(base));
        LOGOS_ASSERT(tv->value.is_pointer(), "HERMES-PARSE-TC-001", "");
        auto* arr = tv->value.as_ptr<ObjectArray>(base);
        LOGOS_ASSERT(arr->size() == 3, "HERMES-PARSE-TC-001", "");
    }

    {
        auto doc = parse_doc("<Varchar, Integer>{name: 1, age: 2}");
        uint8_t* base = HermesAccess::base(doc);
        auto* tv = HermesAccess::root<TypedValueData>(doc);
        auto* dt = tv->datatype.get(base);
        LOGOS_ASSERT(dt->name_view(base) == "Map", "HERMES-PARSE-TC-002", "");
        LOGOS_ASSERT(dt->params.get(base)->size() == 2, "HERMES-PARSE-TC-002", "");
    }

    LOGOS_TRACE("hermes.parse.typed_containers", "status", "pass");
    std::printf("  Parse typed containers: OK\n");
}

// ============================================================================
// Octal literals
// ============================================================================

static void test_parse_octal() {
    std::printf("--- Parse octal literals ---\n");

    {
        // Memoria style: 0 prefix
        auto doc = parse_doc("010");  // octal 8
        auto* root = HermesAccess::root<int32_t>(doc);
        LOGOS_ASSERT(*root == 8, "HERMES-PARSE-OCT-001",
            "010 (octal) must be 8, got {}", *root);
    }
    {
        auto doc = parse_doc("0o77");  // explicit octal
        auto* root = HermesAccess::root<int32_t>(doc);
        LOGOS_ASSERT(*root == 63, "HERMES-PARSE-OCT-001",
            "0o77 must be 63, got {}", *root);
    }

    LOGOS_TRACE("hermes.parse.octal", "status", "pass");
    std::printf("  Parse octal: OK\n");
}

// ============================================================================
// Unicode surrogate pairs
// ============================================================================

static void test_parse_surrogate_pairs() {
    std::printf("--- Parse unicode surrogate pairs ---\n");

    {
        // U+1F600 (😀) encoded as surrogate pair: \uD83D\uDE00
        auto doc = parse_doc("\"\\uD83D\\uDE00\"");
        auto* root = HermesAccess::root<ArenaString>(doc);
        auto sv = root->view();
        // UTF-8 encoding of U+1F600: F0 9F 98 80
        LOGOS_ASSERT(sv.size() == 4, "HERMES-PARSE-SURR-001",
            "Surrogate pair string must be 4 UTF-8 bytes, got {}", sv.size());
        LOGOS_ASSERT(static_cast<uint8_t>(sv[0]) == 0xF0, "HERMES-PARSE-SURR-001", "");
        LOGOS_ASSERT(static_cast<uint8_t>(sv[1]) == 0x9F, "HERMES-PARSE-SURR-001", "");
        LOGOS_ASSERT(static_cast<uint8_t>(sv[2]) == 0x98, "HERMES-PARSE-SURR-001", "");
        LOGOS_ASSERT(static_cast<uint8_t>(sv[3]) == 0x80, "HERMES-PARSE-SURR-001", "");
    }

    LOGOS_TRACE("hermes.parse.surrogate", "status", "pass");
    std::printf("  Parse surrogate pairs: OK\n");
}

// ============================================================================
// Pointer/ref qualifiers
// ============================================================================

static void test_qualifiers() {
    std::printf("--- Qualifiers (*, &, const) ---\n");

    {
        auto doc = parse_doc("int*");
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->ptr_count() == 1, "HERMES-PARSE-QUAL-001",
            "ptr_count must be 1, got {}", dt->ptr_count());
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "int*", "HERMES-PARSE-QUAL-001", "Expected 'int*', got '{}'", s);
    }
    {
        auto doc = parse_doc("int const*");
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->ptr_count() == 1, "HERMES-PARSE-QUAL-002", "");
        LOGOS_ASSERT(dt->is_const(), "HERMES-PARSE-QUAL-002", "");
    }
    {
        auto doc = parse_doc("int&");
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->ref_count() == 1, "HERMES-PARSE-QUAL-003",
            "ref_count must be 1, got {}", dt->ref_count());
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "int&", "HERMES-PARSE-QUAL-003", "Expected 'int&', got '{}'", s);
    }
    {
        auto doc = parse_doc("int&&");
        auto* dt = HermesAccess::root<DatatypeData>(doc);
        LOGOS_ASSERT(dt->ref_count() == 2, "HERMES-PARSE-QUAL-004", "");
        std::string s = stringify(doc).get();
        LOGOS_ASSERT(s == "int&&", "HERMES-PARSE-QUAL-004", "Expected 'int&&', got '{}'", s);
    }

    LOGOS_TRACE("hermes.parse.qualifiers", "status", "pass");
    std::printf("  Qualifiers: OK\n");
}

// ============================================================================
// Round-trip: parse → stringify → parse → compare
// ============================================================================

static void test_round_trip() {
    std::printf("--- Round-trip parse → stringify → parse ---\n");

    const char* inputs[] = {
        "42",
        "\"hello world\"",
        "true",
        "[1, 2, 3]",
        "Array<Integer>",
        "Decimal(10, 2)",
        "@Integer = 42",
        "?myParam",
    };

    for (auto* input : inputs) {
        auto doc1 = parse_doc(input);
        std::string text1 = stringify(doc1).get();
        auto doc2 = parse_doc(text1);
        std::string text2 = stringify(doc2).get();

        LOGOS_ASSERT(text1 == text2, "HERMES-RT-001",
            "Round-trip failed for input '{}': '{}' != '{}'", input, text1, text2);
    }

    LOGOS_TRACE("hermes.roundtrip", "status", "pass");
    std::printf("  Round-trip: OK\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::hermes::hermes_init();
    logos::init_sqlite_sink({.path = "test_traces.sqlite"});

    std::printf("=== Hermes Text Parser & Stringify Exerciser ===\n\n");

    test_parse_integers();
    test_parse_floats();
    test_parse_booleans_null();
    test_parse_strings();
    test_parse_array();
    test_parse_map();
    test_parse_nested();
    test_parse_comments();
    test_parse_type_declarations();
    test_parse_typed_values();
    test_parse_parameters();
    test_parse_typed_containers();
    test_parse_octal();
    test_parse_surrogate_pairs();
    test_qualifiers();
    test_stringify_simple();
    test_stringify_containers();
    test_stringify_compound();
    test_round_trip();

    std::printf("\n=== All text parser tests passed ===\n");

    logos::shutdown_sqlite_sink();
    return 0;
}
