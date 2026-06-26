// Logos project — https://github.com/victor-smirnov/logos
//
// Writ PRODUCER conformance: builds a schema-tagged AST-like node tree exactly the
// way the logosc parser does — make_doc → make_tiny_map (schema_type_code set) →
// put(field, AnyVal) → set_ref to wire children → set_root — then reads it back via
// the owning views + schema_codes category decode. De-risks the §6.2 cut-over of the
// generated parser onto writ2 (the producer surface + the schema_type_code path).

#include <logos/writ/document.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/schema_codes.hpp>
#include <logos/writ/type_codes.hpp>

#include <cstdio>

using namespace logos::writ;

#define CHECK(cond, code) do { if (!(cond)) { std::printf("FAIL %d: %s\n", (code), #cond); return (code); } } while (0)

// AST-like field keys (TinyObjectMap byte keys) + node codes, mirroring the compiler.
namespace la { enum : uint8_t { CODE = 0, NAME = 1, ITEMS = 2, SRC_LINE = 3 }; }
namespace nc { enum : int32_t { MODULE = 100, FN = 101 }; }

// Build one AST-like node: a schema-tagged tiny map { CODE, NAME, SRC_LINE }.
static TinyObjectMap* make_node(WritCtr& doc, int32_t code, std::string_view name, int32_t line) {
    auto* n = *doc.make_tiny_map(4);
    n->set_schema_type_code(schema::ast(code));                     // discriminant in the header
    (void)n->put(la::CODE, AnyVal::from_value(code, tc::HT_I24), doc.arena());
    StringView s = *doc.make_string(name);
    (void)n->put(la::NAME, s.to_anyval(), doc.arena());
    (void)n->put(la::SRC_LINE, AnyVal::from_value(line, tc::HT_U24), doc.arena());
    return n;
}

int main() {
    auto doc_exp = make_doc();                                       // MultiChunk never-move
    CHECK(doc_exp.has_value(), 1);
    WritCtr doc = std::move(*doc_exp);

    // module { NAME="m", ITEMS=[ fn "a", fn "b" ] }
    auto* root = make_node(doc, nc::MODULE, "m", 1);
    ArrayView items = *doc.make_array(2);                            // owning handle
    auto* fa = make_node(doc, nc::FN, "a", 2);
    auto* fb = make_node(doc, nc::FN, "b", 3);
    { AnyVal v; v.set_ref(fa); (void)items.push_back(v); }
    { AnyVal v; v.set_ref(fb); (void)items.push_back(v); }
    (void)root->put(la::ITEMS, items.to_anyval(), doc.arena());
    { AnyVal v; v.set_ref(root); doc.set_root(v); }

    // ── Read back via the owning views (the reader path) ────────────────────────
    TinyMapView rv = as_tinymap(doc.root(), doc.holder());
    CHECK(!rv.is_null(), 2);
    CHECK(rv.ptr()->schema_type_code() == schema::ast(nc::MODULE), 3);
    CHECK(schema::category_of(rv.ptr()->schema_type_code()) == schema::CAT_AST, 4);
    CHECK(schema::variant_of(rv.ptr()->schema_type_code()) == uint64_t(nc::MODULE), 5);
    CHECK(rv.get(la::CODE).as_value<int32_t>() == nc::MODULE, 6);
    CHECK(as_string(rv.get(la::NAME), doc.holder()).view() == "m", 7);

    ArrayView iv = as_array(rv.get(la::ITEMS), doc.holder());
    CHECK(iv.size() == 2, 8);
    TinyMapView c0 = as_tinymap(iv.get(0), doc.holder());
    TinyMapView c1 = as_tinymap(iv.get(1), doc.holder());
    CHECK(c0.ptr()->schema_type_code() == schema::ast(nc::FN), 9);
    CHECK(as_string(c0.get(la::NAME), doc.holder()).view() == "a", 10);
    CHECK(c1.get(la::SRC_LINE).as_value<int32_t>() == 3, 11);
    CHECK(as_string(c1.get(la::NAME), doc.holder()).view() == "b", 12);

    // schema_type_code survives compaction (the .writ0 path), unlike the plain CODE
    // field which also round-trips — both must agree after a compactify.
    doc.seal();

    std::printf("writ producer (AST-like build + schema_type_code + view read-back): OK\n");
    return 0;
}
