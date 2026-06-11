//
// hermes_parser roundtrip test: parses Hermes strings via the generated parser
// and verifies the resulting AST structure.
// Generated hermes_parser.hpp/cpp come from peg_gen run as a build step.

#include "hermes_parser.hpp"

#include <logos/verification/assert.hpp>
#include <logos/hermes/view.hpp>

#include <print>
#include <string_view>

using logos::hermes::HermesParser;
using logos::hermes::Hermes;
using logos::hermes::TinyMapView;
using logos::hermes::ArrayView;
using logos::hermes::StringView;
using logos::hermes::MemHolder;
using logos::hermes::AnyVal;
namespace ha = logos::hermes::hermes_ast;

// ── Navigation helpers ───────────────────────────────────────────────────────

// Root node of a parsed document as TinyMapView.
static TinyMapView root_node(Hermes& doc) {
    return doc.root_object().as_tiny_map();
}

// Discriminant (CODE field) of a TinyObjectMap node.
static int32_t node_code(TinyMapView n) {
    return n.get(ha::CODE).as_value<int32_t>();
}

// String-value field: returns the text stored as an arena string pointer.
static std::string_view node_str(TinyMapView n, logos::NamedCode<uint8_t> field, MemHolder* h) {
    AnyVal v = n.get(field);
    if (v.is_null() || !v.is_pointer()) return {};
    return StringView(v, h).view();
}

// ITEMS array from a MAP or ARRAY node.
static ArrayView node_items(TinyMapView n, MemHolder* h) {
    AnyVal v = n.get(ha::ITEMS);
    return ArrayView(v, h);
}

// ── Scalar tests ─────────────────────────────────────────────────────────────

static void test_integer() {
    HermesParser p("42");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-001", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "42", "ROUND-001", "value=42");
    std::println("  [OK] integer 42");
}

static void test_negative_integer() {
    HermesParser p("-7");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-002", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "-7", "ROUND-002", "value=-7");
    std::println("  [OK] integer -7");
}

static void test_float() {
    HermesParser p("3.14");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-003", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "3.14", "ROUND-003", "value=3.14");
    std::println("  [OK] float 3.14");
}

static void test_float_sci() {
    HermesParser p("1.5e10");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-004", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "1.5e10", "ROUND-004", "value=1.5e10");
    std::println("  [OK] float 1.5e10");
}

static void test_string() {
    // Lexer returns token text verbatim including the surrounding quotes.
    HermesParser p(R"("hello world")");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::STRING), "ROUND-005", "code=STRING");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == R"("hello world")",
        "ROUND-005", "value");
    std::println("  [OK] string");
}

static void test_string_escapes() {
    HermesParser p(R"("a\"b\\c")");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::STRING), "ROUND-006", "code=STRING");
    std::println("  [OK] string with escapes");
}

static void test_bool_true() {
    HermesParser p("true");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::BOOLEAN), "ROUND-007", "code=BOOLEAN");
    // Codegen emits AnyVal::from_value(uint8_t(1)) for true.
    LOGOS_ASSERT(root.get(ha::VALUE).as_value<uint8_t>() == 1, "ROUND-007", "true=1");
    std::println("  [OK] bool true");
}

static void test_bool_false() {
    HermesParser p("false");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::BOOLEAN), "ROUND-008", "code=BOOLEAN");
    LOGOS_ASSERT(root.get(ha::VALUE).as_value<uint8_t>() == 0, "ROUND-008", "false=0");
    std::println("  [OK] bool false");
}

static void test_null() {
    HermesParser p("null");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::NULL_VAL), "ROUND-009", "code=NULL_VAL");
    std::println("  [OK] null");
}

// ── Map tests ─────────────────────────────────────────────────────────────────

static void test_empty_map() {
    HermesParser p("{}");
    auto doc = p.parse_map();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::MAP), "ROUND-010", "code=MAP");
    auto items = node_items(root, doc.holder());
    LOGOS_ASSERT(items.size() == 0, "ROUND-010", "empty map: 0 items, got {}", items.size());
    std::println("  [OK] empty map");
}

static void test_single_entry_map() {
    HermesParser p(R"({"x": 1})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::MAP), "ROUND-011", "code=MAP");
    auto items = node_items(root, h);
    LOGOS_ASSERT(items.size() == 1, "ROUND-011", "1 entry, got {}", items.size());

    auto entry = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_code(entry) == int32_t(ha::MAP_ENTRY), "ROUND-011", "entry code");
    LOGOS_ASSERT(node_str(entry, ha::KEY, h) == R"("x")", "ROUND-011", "key");

    auto val = TinyMapView(entry.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(val) == int32_t(ha::INTEGER), "ROUND-011", "value is INTEGER");
    LOGOS_ASSERT(node_str(val, ha::VALUE, h) == "1", "ROUND-011", "value=1");
    std::println("  [OK] single-entry map");
}

static void test_multi_entry_map() {
    HermesParser p(R"({"a": 1, "b": 2, "c": 3})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 3, "ROUND-012", "3 entries, got {}", items.size());

    std::string_view keys[] = {R"("a")", R"("b")", R"("c")"};
    std::string_view vals[] = {"1", "2", "3"};
    for (uint64_t i = 0; i < 3; ++i) {
        auto entry = TinyMapView(items.get(i), h);
        LOGOS_ASSERT(node_str(entry, ha::KEY, h) == keys[i], "ROUND-012", "key[{}]", i);
        auto val = TinyMapView(entry.get(ha::VALUE), h);
        LOGOS_ASSERT(node_str(val, ha::VALUE, h) == vals[i], "ROUND-012", "val[{}]", i);
    }
    std::println("  [OK] multi-entry map");
}

static void test_ident_key_map() {
    HermesParser p("{x: 42, y: -1}");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-013", "2 entries");

    auto e0 = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_str(e0, ha::KEY, h) == "x", "ROUND-013", "key[0]=x");

    auto e1 = TinyMapView(items.get(1), h);
    LOGOS_ASSERT(node_str(e1, ha::KEY, h) == "y", "ROUND-013", "key[1]=y");
    auto v1 = TinyMapView(e1.get(ha::VALUE), h);
    LOGOS_ASSERT(node_str(v1, ha::VALUE, h) == "-1", "ROUND-013", "val[1]=-1");
    std::println("  [OK] ident-key map");
}

static void test_map_trailing_comma() {
    HermesParser p(R"({"a": 1, "b": 2,})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-014", "2 entries with trailing comma");
    std::println("  [OK] map trailing comma");
}

static void test_map_mixed_value_types() {
    HermesParser p(R"({"n": 1, "s": "hi", "b": true, "nil": null})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 4, "ROUND-015", "4 entries");

    auto check_code = [&](uint64_t i, int32_t expected_code) {
        auto entry = TinyMapView(items.get(i), h);
        auto val = TinyMapView(entry.get(ha::VALUE), h);
        LOGOS_ASSERT(node_code(val) == expected_code, "ROUND-015", "entry[{}] code", i);
    };
    check_code(0, int32_t(ha::INTEGER));
    check_code(1, int32_t(ha::STRING));
    check_code(2, int32_t(ha::BOOLEAN));
    check_code(3, int32_t(ha::NULL_VAL));
    std::println("  [OK] map mixed value types");
}

// ── Array tests ───────────────────────────────────────────────────────────────

static void test_empty_array() {
    HermesParser p("[]");
    auto doc = p.parse_array();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::ARRAY), "ROUND-020", "code=ARRAY");
    auto items = node_items(root, doc.holder());
    LOGOS_ASSERT(items.size() == 0, "ROUND-020", "empty array");
    std::println("  [OK] empty array");
}

static void test_integer_array() {
    HermesParser p("[1, 2, 3]");
    auto doc = p.parse_array();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 3, "ROUND-021", "3 items, got {}", items.size());

    std::string_view expected[] = {"1", "2", "3"};
    for (uint64_t i = 0; i < 3; ++i) {
        auto elem = TinyMapView(items.get(i), h);
        LOGOS_ASSERT(node_code(elem) == int32_t(ha::INTEGER), "ROUND-021", "elem[{}] INTEGER", i);
        LOGOS_ASSERT(node_str(elem, ha::VALUE, h) == expected[i], "ROUND-021", "elem[{}]", i);
    }
    std::println("  [OK] integer array [1, 2, 3]");
}

static void test_array_trailing_comma() {
    HermesParser p("[1, 2,]");
    auto doc = p.parse_array();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-022", "2 items with trailing comma");
    std::println("  [OK] array trailing comma");
}

static void test_mixed_array() {
    HermesParser p(R"([1, "two", true, null])");
    auto doc = p.parse_array();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 4, "ROUND-023", "4 items");
    int32_t expected_codes[] = {
        int32_t(ha::INTEGER), int32_t(ha::STRING),
        int32_t(ha::BOOLEAN), int32_t(ha::NULL_VAL)
    };
    for (uint64_t i = 0; i < 4; ++i) {
        auto elem = TinyMapView(items.get(i), h);
        LOGOS_ASSERT(node_code(elem) == expected_codes[i], "ROUND-023", "elem[{}] code", i);
    }
    std::println("  [OK] mixed array");
}

// ── Nesting tests ─────────────────────────────────────────────────────────────

static void test_nested_map_in_array() {
    HermesParser p(R"([{"x": 1}, {"y": 2}])");
    auto doc = p.parse_array();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-030", "2 map items in array");

    auto m0 = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_code(m0) == int32_t(ha::MAP), "ROUND-030", "elem[0] is MAP");
    auto m0_items = node_items(m0, h);
    LOGOS_ASSERT(m0_items.size() == 1, "ROUND-030", "map[0] has 1 entry");

    auto e0 = TinyMapView(m0_items.get(0), h);
    LOGOS_ASSERT(node_str(e0, ha::KEY, h) == R"("x")", "ROUND-030", "key=x");
    std::println("  [OK] nested map-in-array");
}

static void test_array_in_map() {
    HermesParser p(R"({"items": [1, 2], "count": 2})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto top = node_items(root_node(doc), h);
    LOGOS_ASSERT(top.size() == 2, "ROUND-031", "2 top entries");

    auto e0 = TinyMapView(top.get(0), h);
    LOGOS_ASSERT(node_str(e0, ha::KEY, h) == R"("items")", "ROUND-031", "key=items");
    auto arr = TinyMapView(e0.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(arr) == int32_t(ha::ARRAY), "ROUND-031", "value is ARRAY");
    LOGOS_ASSERT(node_items(arr, h).size() == 2, "ROUND-031", "array has 2 elems");

    auto e1 = TinyMapView(top.get(1), h);
    LOGOS_ASSERT(node_str(e1, ha::KEY, h) == R"("count")", "ROUND-031", "key=count");
    std::println("  [OK] array-in-map");
}

// ── Typed value tests ─────────────────────────────────────────────────────────

static void test_typed_value_simple() {
    HermesParser p(R"(Date("2026-01-01"))");
    auto doc = p.parse_typed_value();
    auto h = doc.holder();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::TYPED_VALUE), "ROUND-040", "code=TYPED_VALUE");

    auto name_node = TinyMapView(root.get(ha::NAME), h);
    LOGOS_ASSERT(node_code(name_node) == int32_t(ha::DATATYPE), "ROUND-040", "NAME is DATATYPE");
    LOGOS_ASSERT(node_str(name_node, ha::NAME, h) == "Date", "ROUND-040", "typename=Date");

    auto val_node = TinyMapView(root.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(val_node) == int32_t(ha::STRING), "ROUND-040", "value is STRING");
    std::println("  [OK] typed value Date(...)");
}

static void test_typed_value_integer_arg() {
    HermesParser p("Meters(100)");
    auto doc = p.parse_typed_value();
    auto h = doc.holder();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::TYPED_VALUE), "ROUND-041", "TYPED_VALUE");

    auto name_node = TinyMapView(root.get(ha::NAME), h);
    LOGOS_ASSERT(node_str(name_node, ha::NAME, h) == "Meters", "ROUND-041", "typename=Meters");

    auto val_node = TinyMapView(root.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(val_node) == int32_t(ha::INTEGER), "ROUND-041", "value is INTEGER");
    LOGOS_ASSERT(node_str(val_node, ha::VALUE, h) == "100", "ROUND-041", "value=100");
    std::println("  [OK] typed value Meters(100)");
}

// ── Whitespace and comments ───────────────────────────────────────────────────

static void test_whitespace_tolerance() {
    HermesParser p("  {  \"a\"  :  1  ,  \"b\"  :  2  }  ");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-050", "2 entries despite extra whitespace");
    std::println("  [OK] whitespace tolerance");
}

static void test_comment_skipped() {
    HermesParser p("// ignored\n42");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-051", "comment skipped");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "42", "ROUND-051", "value=42");
    std::println("  [OK] line comment skipped");
}

static void test_block_comment_skipped() {
    HermesParser p("/* leading */ 99 /* trailing */");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-052", "block comment skipped");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "99", "ROUND-052", "value=99");
    std::println("  [OK] block comment skipped");
}

static void test_block_comment_inline() {
    // Block comment mid-expression: key /* ignored */ : value
    HermesParser p(R"({/* c */ "k" /* c */: /* c */ 1 /* c */})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 1, "ROUND-053", "1 entry despite inline block comments");
    auto entry = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_str(entry, ha::KEY, h) == R"("k")", "ROUND-053", "key=k");
    std::println("  [OK] block comment inline");
}

// ── Number edge cases ────────────────────────────────────────────────────────

static void test_negative_float() {
    HermesParser p("-3.14");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-060", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "-3.14", "ROUND-060", "value=-3.14");
    std::println("  [OK] negative float -3.14");
}

static void test_float_dot_leading() {
    // FLOAT regex allows [0-9]* before dot, so .5 is valid.
    HermesParser p(".5");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-061", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == ".5", "ROUND-061", "value=.5");
    std::println("  [OK] dot-leading float .5");
}

static void test_float_sci_neg_exp() {
    HermesParser p("1.5e-3");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-062", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "1.5e-3", "ROUND-062", "value=1.5e-3");
    std::println("  [OK] float 1.5e-3");
}

static void test_zero() {
    HermesParser p("0");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-063", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0", "ROUND-063", "value=0");
    std::println("  [OK] integer 0");
}

// ── Typed integer suffixes ────────────────────────────────────────────────────

static void test_integer_u8() {
    HermesParser p("255_u8");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-064", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "255_u8", "ROUND-064", "value=255_u8");
    std::println("  [OK] integer 255_u8");
}

static void test_integer_s64() {
    HermesParser p("-100_s64");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-065", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "-100_s64", "ROUND-065", "value=-100_s64");
    std::println("  [OK] integer -100_s64");
}

static void test_integer_ull() {
    HermesParser p("100ull");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-066", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "100ull", "ROUND-066", "value=100ull");
    std::println("  [OK] integer 100ull");
}

static void test_integer_ll() {
    HermesParser p("100ll");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-067", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "100ll", "ROUND-067", "value=100ll");
    std::println("  [OK] integer 100ll");
}

static void test_integer_u() {
    HermesParser p("42u");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-068", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "42u", "ROUND-068", "value=42u");
    std::println("  [OK] integer 42u");
}

// ── Hex / binary / octal ─────────────────────────────────────────────────────

static void test_hex_integer() {
    HermesParser p("0xFF");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-073", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0xFF", "ROUND-073", "value=0xFF");
    std::println("  [OK] integer 0xFF");
}

static void test_hex_with_suffix() {
    HermesParser p("0xFF_u32");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-074", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0xFF_u32", "ROUND-074", "value=0xFF_u32");
    std::println("  [OK] integer 0xFF_u32");
}

static void test_binary_integer() {
    HermesParser p("0b1010");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-075", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0b1010", "ROUND-075", "value=0b1010");
    std::println("  [OK] integer 0b1010");
}

static void test_binary_with_suffix() {
    HermesParser p("0b1010_u16");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-076", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0b1010_u16", "ROUND-076", "value=0b1010_u16");
    std::println("  [OK] integer 0b1010_u16");
}

static void test_octal_integer() {
    HermesParser p("0o17");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::INTEGER), "ROUND-077", "code=INTEGER");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "0o17", "ROUND-077", "value=0o17");
    std::println("  [OK] integer 0o17");
}

// ── Float suffixes ───────────────────────────────────────────────────────────

static void test_float_f_suffix() {
    HermesParser p("3.14f");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-078", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "3.14f", "ROUND-078", "value=3.14f");
    std::println("  [OK] float 3.14f");
}

static void test_float_d_suffix() {
    HermesParser p("2.718d");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-079", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "2.718d", "ROUND-079", "value=2.718d");
    std::println("  [OK] float 2.718d");
}

static void test_float_sci_f_suffix() {
    HermesParser p("1.5e10f");
    auto doc = p.parse_value();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::FLOAT), "ROUND-079b", "code=FLOAT");
    LOGOS_ASSERT(node_str(root, ha::VALUE, doc.holder()) == "1.5e10f", "ROUND-079b", "value=1.5e10f");
    std::println("  [OK] float 1.5e10f");
}

// ── Keyword boundary ─────────────────────────────────────────────────────────

static void test_keyword_prefix_ident() {
    // "trueish" must lex as IDENT, not TRUE + IDENT("ish")
    HermesParser p("{trueish: 1}");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 1, "ROUND-070", "1 entry");
    auto entry = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_str(entry, ha::KEY, h) == "trueish", "ROUND-070", "key=trueish");
    std::println("  [OK] keyword-prefix ident 'trueish'");
}

static void test_keyword_prefix_null() {
    // "nullify" must lex as IDENT, not NULL + IDENT("ify")
    HermesParser p("{nullify: 1}");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 1, "ROUND-071", "1 entry");
    auto entry = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_str(entry, ha::KEY, h) == "nullify", "ROUND-071", "key=nullify");
    std::println("  [OK] keyword-prefix ident 'nullify'");
}

// ── Parameterized types ──────────────────────────────────────────────────────

static void test_typed_value_single_param() {
    // List<Int>(42)
    HermesParser p("List<Int>(42)");
    auto doc = p.parse_typed_value();
    auto h = doc.holder();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::TYPED_VALUE), "ROUND-080", "TYPED_VALUE");

    auto dt = TinyMapView(root.get(ha::NAME), h);
    LOGOS_ASSERT(node_str(dt, ha::NAME, h) == "List", "ROUND-080", "name=List");

    auto params_val = dt.get(ha::PARAMS);
    LOGOS_ASSERT(!params_val.is_null() && params_val.is_pointer(), "ROUND-080", "PARAMS exists");
    auto params = ArrayView(params_val, h);
    LOGOS_ASSERT(params.size() == 1, "ROUND-080", "1 type param, got {}", params.size());

    auto p0 = TinyMapView(params.get(0), h);
    LOGOS_ASSERT(node_code(p0) == int32_t(ha::DATATYPE), "ROUND-080", "param[0] is DATATYPE");
    LOGOS_ASSERT(node_str(p0, ha::NAME, h) == "Int", "ROUND-080", "param[0]=Int");
    std::println("  [OK] typed value List<Int>(42)");
}

static void test_typed_value_multi_param() {
    // Map<String, Int>({"a": 1})
    HermesParser p(R"(Map<String, Int>({"a": 1}))");
    auto doc = p.parse_typed_value();
    auto h = doc.holder();
    auto root = root_node(doc);
    LOGOS_ASSERT(node_code(root) == int32_t(ha::TYPED_VALUE), "ROUND-081", "TYPED_VALUE");

    auto dt = TinyMapView(root.get(ha::NAME), h);
    LOGOS_ASSERT(node_str(dt, ha::NAME, h) == "Map", "ROUND-081", "name=Map");

    auto params = ArrayView(dt.get(ha::PARAMS), h);
    LOGOS_ASSERT(params.size() == 2, "ROUND-081", "2 type params, got {}", params.size());

    auto p0 = TinyMapView(params.get(0), h);
    LOGOS_ASSERT(node_str(p0, ha::NAME, h) == "String", "ROUND-081", "param[0]=String");
    auto p1 = TinyMapView(params.get(1), h);
    LOGOS_ASSERT(node_str(p1, ha::NAME, h) == "Int", "ROUND-081", "param[1]=Int");
    std::println("  [OK] typed value Map<String, Int>(...)");
}

static void test_typed_value_nested_param() {
    // Map<String, List<Int>>({"a": [1]})
    HermesParser p(R"(Map<String, List<Int>>({"a": [1]}))");
    auto doc = p.parse_typed_value();
    auto h = doc.holder();
    auto root = root_node(doc);

    auto dt = TinyMapView(root.get(ha::NAME), h);
    LOGOS_ASSERT(node_str(dt, ha::NAME, h) == "Map", "ROUND-082", "name=Map");

    auto params = ArrayView(dt.get(ha::PARAMS), h);
    LOGOS_ASSERT(params.size() == 2, "ROUND-082", "2 type params");

    // Second param: List<Int>
    auto p1 = TinyMapView(params.get(1), h);
    LOGOS_ASSERT(node_str(p1, ha::NAME, h) == "List", "ROUND-082", "param[1]=List");

    auto inner_params = ArrayView(p1.get(ha::PARAMS), h);
    LOGOS_ASSERT(inner_params.size() == 1, "ROUND-082", "List has 1 param");
    auto inner_p0 = TinyMapView(inner_params.get(0), h);
    LOGOS_ASSERT(node_str(inner_p0, ha::NAME, h) == "Int", "ROUND-082", "inner param=Int");
    std::println("  [OK] typed value Map<String, List<Int>>(...)");
}

// ── Typed values in compound contexts ────────────────────────────────────────

static void test_typed_value_in_map() {
    HermesParser p(R"({"d": Date("2026-01-01"), "n": 42})");
    auto doc = p.parse_map();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-090", "2 entries");

    auto e0 = TinyMapView(items.get(0), h);
    auto v0 = TinyMapView(e0.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(v0) == int32_t(ha::TYPED_VALUE), "ROUND-090", "val[0] TYPED_VALUE");

    auto e1 = TinyMapView(items.get(1), h);
    auto v1 = TinyMapView(e1.get(ha::VALUE), h);
    LOGOS_ASSERT(node_code(v1) == int32_t(ha::INTEGER), "ROUND-090", "val[1] INTEGER");
    std::println("  [OK] typed value in map");
}

static void test_typed_value_in_array() {
    HermesParser p(R"([Date("2026-01-01"), Meters(100)])");
    auto doc = p.parse_array();
    auto h = doc.holder();
    auto items = node_items(root_node(doc), h);
    LOGOS_ASSERT(items.size() == 2, "ROUND-091", "2 elements");

    auto e0 = TinyMapView(items.get(0), h);
    LOGOS_ASSERT(node_code(e0) == int32_t(ha::TYPED_VALUE), "ROUND-091", "elem[0] TYPED_VALUE");
    auto e1 = TinyMapView(items.get(1), h);
    LOGOS_ASSERT(node_code(e1) == int32_t(ha::TYPED_VALUE), "ROUND-091", "elem[1] TYPED_VALUE");
    std::println("  [OK] typed values in array");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    std::println("── hermes_parser roundtrip ──────────────────────────────");

    std::println("scalars:");
    test_integer();
    test_negative_integer();
    test_float();
    test_float_sci();
    test_string();
    test_string_escapes();
    test_bool_true();
    test_bool_false();
    test_null();

    std::println("maps:");
    test_empty_map();
    test_single_entry_map();
    test_multi_entry_map();
    test_ident_key_map();
    test_map_trailing_comma();
    test_map_mixed_value_types();

    std::println("arrays:");
    test_empty_array();
    test_integer_array();
    test_array_trailing_comma();
    test_mixed_array();

    std::println("nesting:");
    test_nested_map_in_array();
    test_array_in_map();

    std::println("typed values:");
    test_typed_value_simple();
    test_typed_value_integer_arg();

    std::println("lexer:");
    test_whitespace_tolerance();
    test_comment_skipped();
    test_block_comment_skipped();
    test_block_comment_inline();

    std::println("number edge cases:");
    test_negative_float();
    test_float_dot_leading();
    test_float_sci_neg_exp();
    test_zero();

    std::println("typed integer suffixes:");
    test_integer_u8();
    test_integer_s64();
    test_integer_ull();
    test_integer_ll();
    test_integer_u();

    std::println("hex / binary / octal:");
    test_hex_integer();
    test_hex_with_suffix();
    test_binary_integer();
    test_binary_with_suffix();
    test_octal_integer();

    std::println("float suffixes:");
    test_float_f_suffix();
    test_float_d_suffix();
    test_float_sci_f_suffix();

    std::println("keyword boundary:");
    test_keyword_prefix_ident();
    test_keyword_prefix_null();

    std::println("parameterized types:");
    test_typed_value_single_param();
    test_typed_value_multi_param();
    test_typed_value_nested_param();

    std::println("typed values in context:");
    test_typed_value_in_map();
    test_typed_value_in_array();

    std::println();
    std::println("all roundtrip tests passed.");
    return 0;
}
