// Logos project — https://github.com/victor-smirnov/logos

#include "codegen.hpp"
#include "grammar_ast.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <format>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs  = std::filesystem;
namespace ast = logos::peg_gen::ast;

using logos::writ::AnyVal;
using logos::writ::ArrayView;
using logos::writ::MapView;
using logos::writ::TinyMapView;
using logos::writ::StringView;
using logos::writ::MemHolder;

namespace logos::peg_gen {

// ═══════════════════════════════════════════════════════════════════════════
// Plain C++ structs — intermediate representation extracted from grammar doc.
// Keeps the codegen itself free of Writ navigation.
// ═══════════════════════════════════════════════════════════════════════════

struct NameDecl  { std::string name; int32_t code; std::string group; std::string doc; };
// output = base name of the imported module's generated files. `ns`/`name` are
// ITS OWN %meta namespace and name (→ its Parser class); both were missing, so
// the emitter qualified the sub-parser with the IMPORTING grammar's namespace —
// naming a class that does not exist there. `arena_ext` gates the embed: only a
// borrowed-arena parser can build into the importer's document.
struct ImportRef {
    std::string alias, output, ns, name;
    bool        arena_ext = false;
};

struct TokenDecl {
    std::string name;
    int32_t     kind;   // ast::TOKEN_LITERAL / TOKEN_REGEX / TOKEN_SKIP
    std::string pattern;
};

struct PrecLevel {
    int32_t                  assoc;  // ast::ASSOC_LEFT / RIGHT / NONE
    std::vector<std::string> tokens;
};

// Action expression inside => { FIELD: expr }
struct ActionExpr {
    int32_t     kind;        // ast::CAPTURE / ARRAY_CAPTURE / INT_LIT / STR_LIT / BOOL_LIT
    int32_t     index = 0;   // for CAPTURE: $n
    std::string value;       // for STR_LIT / symbolic name
    int32_t     int_val = 0; // for INT_LIT / BOOL_LIT
};

struct ActionField { std::string name; ActionExpr expr; };
struct Action      { std::vector<ActionField> fields; };

struct Item {
    int32_t     kind;           // ast::RULE_REF / TOKEN_REF / LITERAL / OPT / REP / GROUP / ...
    std::string name;           // rule/token name or literal text
    std::string grammar_alias;  // for cross-grammar RULE_REF
    int32_t     min = 1, max = 1; // for REP
    // Children: for GROUP → sub_alts; for OPT/REP/LOOKAHEAD/NEG_AHEAD → sub_items[0]
    std::vector<Item> sub_items;

    struct Alt;
    std::vector<Alt> sub_alts;
};

struct Item::Alt {
    std::vector<Item>         seq;
    std::optional<Action>     action;
};

struct Rule {
    std::string            name;
    std::string            group;   // rule's group (empty = none)
    std::vector<Item::Alt> alts;
};

// %schema — one declared field of a typed-Writ node. `key` is the explicit TOM
// key (`field: "ty" = N`); `has_key` is false when the grammar omitted it, which
// the Logos backend tolerates (logosc resolves field→key against the real
// `schema` item) but the C++ backend cannot — see emit_schema_action.
struct SchemaField {
    std::string name;
    std::string ftype;          // "ref T" | "fan set_x MAX" | "argfan" | "str" | "bool" | "WAny" | scalar
    int32_t     key = 0;
    bool        has_key = false;

    // A fan field is not a TOM slot: it spreads an ARRAY_CAPTURE across the
    // node's slot fields (+ a count), so it carries no key of its own.
    bool is_fan() const {
        return ftype == "argfan" || ftype.rfind("fan ", 0) == 0;
    }
    bool is_ref() const { return ftype.rfind("ref ", 0) == 0; }

    // "ref TARGET" → TARGET
    std::string ref_target() const {
        return is_ref() ? ftype.substr(4) : std::string{};
    }
    // "fan <setter> <maxfn> <cap>" → cap. The C++ backend writes TOM slots
    // directly (it has no `set_arg`/`set_len` methods), so it needs the slot
    // COUNT spelled out; both backends stop reading <maxfn> at the space, so
    // the trailing <cap> is inert on the Logos side. -1 = not spelled.
    int fan_cap() const {
        if (!is_fan()) return -1;
        // fields: "fan" <setter> <maxfn> <cap>
        size_t i = 0, tok = 0;
        while (i < ftype.size()) {
            while (i < ftype.size() && ftype[i] == ' ') ++i;
            size_t b = i;
            while (i < ftype.size() && ftype[i] != ' ') ++i;
            if (b == i) break;
            if (++tok == 4) {
                int v = 0;
                for (size_t k = b; k < i; ++k) {
                    if (ftype[k] < '0' || ftype[k] > '9') return -1;
                    v = v * 10 + (ftype[k] - '0');
                }
                return v;
            }
        }
        return -1;
    }
};
struct SchemaDecl {
    std::string              name;
    uint64_t                 type_code = 0;   // ADR-0011 `code(0x…)`
    bool                     has_type_code = false;
    // TOM slot capacity = the field count of the mirrored `schema` item, NOT of
    // this block. The grammar declares only what it writes; a later pass writes
    // more, and the map's capacity is fixed at construction.
    int32_t                  cap = 0;
    bool                     has_cap = false;
    std::vector<SchemaField> fields;

    const SchemaField* find(const std::string& n) const {
        for (const auto& f : fields) if (f.name == n) return &f;
        return nullptr;
    }
};

// Captures map 1:1 onto sequence position: `captures[i]` is produced by
// `seq[i-1]`. So "was this capture a literal INTEGER/FLOAT/STRING token?" is
// answerable at GENERATION time from the .peg alone — no runtime tagging.
// Mirrors peg_gen_logos's capture_is_token_named (codegen.logos:367-374), and
// it is exactly the condition under which the emitted `Token tok_<cap>_` local
// is in scope: a bare TOKEN_REF at top level. Anything wrapped in OPT/REP/GROUP
// yields a non-TOKEN_REF seq item here, so this returns false and the caller
// falls back to the value-only path — the two facts stay in lockstep by
// construction.
static bool capture_is_token_named(const std::vector<Item>& seq, size_t capidx,
                                   std::string_view tok) {
    if (capidx < 1 || capidx > seq.size()) return false;
    const Item& it = seq[capidx - 1];
    return it.kind == int32_t(ast::TOKEN_REF) && it.name == tok;
}

// The 1-based capture index of the last VALUE-carrying item in a no-action alt:
// the last item that is a RULE_REF / OPT / REP / GROUP (a node handle). Skipped:
// bare TOKEN_REF / LITERAL delimiters (captures are only interned strings) and
// LOOKAHEAD / NEG_AHEAD (carry no value at all — and their cap is declared
// inside a nested block, so it isn't even in scope at the passthrough). Returns
// 0 when the alt has no value-carrying item.
//
// This is what makes the `( expr )` grouping alt return the inner expr rather
// than the `)` token. Ported from peg_gen_logos's last_value_capture
// (codegen.logos:2669-2678), which the C++ backend never had — it returned the
// LAST capture unconditionally, and nothing noticed because logos.peg has no
// no-action alt ending in a delimiter.
static size_t last_value_capture(const std::vector<Item>& seq) {
    for (size_t i = seq.size(); i-- > 0;) {
        int32_t k = seq[i].kind;
        if (k == int32_t(ast::TOKEN_REF) || k == int32_t(ast::LITERAL)
            || k == int32_t(ast::LOOKAHEAD) || k == int32_t(ast::NEG_AHEAD))
            continue;
        return i + 1;
    }
    return 0;
}

// An `INTEGER?` capture is still token TEXT — just interned into the arena
// instead of left in a `Token` local, and null when the option did not match.
// So the value HAS a defined encoding, unlike a capture of a rule (an AST node).
// Distinguishing the two is what lets the scalar path decode the first and
// reject the second, where the Logos backend silently decodes both.
static bool capture_is_opt_token(const std::vector<Item>& seq, size_t capidx) {
    if (capidx < 1 || capidx > seq.size()) return false;
    const Item& it = seq[capidx - 1];
    return it.kind == int32_t(ast::OPT) && !it.sub_items.empty()
        && it.sub_items[0].kind == int32_t(ast::TOKEN_REF);
}

struct GrammarInfo {
    // %meta
    std::string name, cxx_namespace, output;

    // %import (already resolved — just alias + output base name)
    std::vector<ImportRef> imports;

    // %export
    std::vector<std::string> exports;

    // %fields / %nodes
    std::vector<NameDecl> fields, nodes;

    // %tokens
    std::vector<TokenDecl> tokens;

    // %prec
    std::vector<PrecLevel> prec;

    // %rules
    std::vector<Rule> rules;

    // %schema — the typed-Writ dialect. A NON-EMPTY `schemas` IS the mode switch
    // (mirrors peg_gen_logos's cg.schema_mode): actions then name a quoted schema
    // type in CODE and address that type's declared fields by name, and node
    // construction routes through emit_schema_action instead of emit_action.
    // Absent ⇒ raw-TOM mode, byte-identical to before (keeps the oracle green).
    std::vector<SchemaDecl> schemas;
    std::string             schema_use;   // space-joined `use:` module paths
    bool                    arena_ext = false;
    std::string             ref_wrap;     // Logos-only edge wrapper; C++ emits ref AnyVals

    bool schema_mode() const { return !schemas.empty(); }

    const SchemaDecl* find_schema(const std::string& n) const {
        for (const auto& s : schemas) if (s.name == n) return &s;
        return nullptr;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// GrammarReader — navigates Writ grammar document → GrammarInfo
// ═══════════════════════════════════════════════════════════════════════════

static std::string read_str(AnyVal val, MemHolder* h) {
    if (val.is_null() || !val.is_pointer()) return {};
    return std::string(StringView(val, h).view());
}

static int32_t read_int(AnyVal val) {
    if (val.is_null() || !val.is_value()) return 0;
    return val.as_value<int32_t>();
}

static std::string to_pascal(const std::string& snake) {
    std::string result;
    bool cap = true;
    for (char c : snake) {
        if (c == '_') { cap = true; }
        else { result += cap ? char(std::toupper(c)) : c; cap = false; }
    }
    return result;
}

class GrammarReader {
public:
    static GrammarInfo read(const logos::writ::WritView& doc,
                            const std::vector<ResolvedModule>&  all_modules) {
        GrammarInfo g;
        MemHolder* h = doc.holder();

        auto root_obj = doc.root_object();
        if (root_obj.is_null()) return g;
        MapView root = root_obj.as_map();

        read_meta(root, h, g);
        read_exports(root, h, g);
        read_name_decls(root, "fields", h, g.fields);
        read_name_decls(root, "nodes",  h, g.nodes);
        read_tokens(root, h, g);
        read_prec(root, h, g);
        read_rules(root, h, g);
        read_imports(root, h, all_modules, g);
        read_schema(root, h, g);
        return g;
    }

    // %schema → g.schemas + the block's header settings.
    static void read_schema(MapView& root, MemHolder* h, GrammarInfo& g) {
        g.schema_use = read_str(root.get("schema_use"), h);
        g.ref_wrap   = read_str(root.get("schema_ref_wrap"), h);
        AnyVal ae = root.get("schema_arena_ext");
        g.arena_ext = !ae.is_null() && ae.is_value() && ae.as_value<bool>();

        AnyVal arr_val = root.get("schema");
        if (arr_val.is_null() || !arr_val.is_pointer()) return;
        auto arr = logos::writ::as_array(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal el = arr.get(i);
            if (!el.is_pointer()) continue;
            auto node = logos::writ::as_tinymap(el, h);

            SchemaDecl sd;
            sd.name = read_str(node.get(uint8_t(ast::NAME)), h);

            AnyVal tc = node.get(uint8_t(ast::TYPE_CODE));
            if (!tc.is_null() && tc.is_value()) {
                sd.type_code = tc.as_value<uint64_t>();
                sd.has_type_code = true;
            }
            AnyVal sc = node.get(uint8_t(ast::SCAP));
            if (!sc.is_null() && sc.is_value()) {
                sd.cap = sc.as_value<int32_t>();
                sd.has_cap = true;
            }

            AnyVal fields_val = node.get(uint8_t(ast::FIELDS));
            if (fields_val.is_pointer()) {
                auto farr = logos::writ::as_array(fields_val, h);
                for (uint64_t j = 0; j < farr.size(); ++j) {
                    AnyVal fel = farr.get(j);
                    if (!fel.is_pointer()) continue;
                    auto fn = logos::writ::as_tinymap(fel, h);
                    SchemaField sf;
                    sf.name  = read_str(fn.get(uint8_t(ast::NAME)),  h);
                    sf.ftype = read_str(fn.get(uint8_t(ast::FTYPE)), h);
                    AnyVal k = fn.get(uint8_t(ast::FKEY));
                    if (!k.is_null() && k.is_value()) {
                        sf.key = k.as_value<int32_t>();
                        sf.has_key = true;
                    }
                    sd.fields.push_back(std::move(sf));
                }
            }
            g.schemas.push_back(std::move(sd));
        }
    }

private:
    static void read_meta(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal meta_val = root.get("meta");
        if (meta_val.is_null()) return;
        TinyMapView meta(meta_val, h);
        g.name          = read_str(meta.get(uint8_t(ast::NAME)),      h);
        g.cxx_namespace = read_str(meta.get(uint8_t(ast::NAMESPACE)), h);
        g.output        = read_str(meta.get(uint8_t(ast::OUTPUT)),    h);
    }

    static void read_exports(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("exports");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (!elem.is_null()) g.exports.push_back(read_str(elem, h));
        }
    }

    static void read_name_decls(MapView& root, const char* key, MemHolder* h,
                                std::vector<NameDecl>& out) {
        AnyVal arr_val = root.get(key);
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem, h);
            int32_t code = read_int(node.get(uint8_t(ast::CODE)));
            if (code == int32_t(ast::GROUP_DECL)) {
                // Group block: recurse into FIELDS, tag each entry with the group name.
                std::string gname = read_str(node.get(uint8_t(ast::NAME)), h);
                AnyVal fields_av = node.get(uint8_t(ast::FIELDS));
                if (fields_av.is_null()) continue;
                ArrayView fields(fields_av, h);
                for (uint64_t j = 0; j < fields.size(); ++j) {
                    AnyVal fe = fields.get(j);
                    if (fe.is_null()) continue;
                    TinyMapView fn(fe, h);
                    out.push_back({
                        read_str(fn.get(uint8_t(ast::NAME)), h),
                        read_int(fn.get(uint8_t(ast::VALUE))),
                        gname,
                        read_str(fn.get(uint8_t(ast::DOC)), h)
                    });
                }
            } else {
                out.push_back({
                    read_str(node.get(uint8_t(ast::NAME)),  h),
                    read_int(node.get(uint8_t(ast::VALUE))),
                    "",  // global
                    read_str(node.get(uint8_t(ast::DOC)), h)
                });
            }
        }
    }

    static void read_tokens(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("tokens");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem, h);
            g.tokens.push_back({
                read_str(node.get(uint8_t(ast::NAME)),    h),
                read_int(node.get(uint8_t(ast::KIND))),
                read_str(node.get(uint8_t(ast::PATTERN)), h)
            });
        }
    }

    static void read_prec(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("prec");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem, h);
            PrecLevel level;
            level.assoc = read_int(node.get(uint8_t(ast::ASSOC)));
            AnyVal toks_val = node.get(uint8_t(ast::TOKENS));
            if (!toks_val.is_null()) {
                ArrayView toks(toks_val, h);
                for (uint64_t j = 0; j < toks.size(); ++j)
                    level.tokens.push_back(read_str(toks.get(j), h));
            }
            g.prec.push_back(std::move(level));
        }
    }

    static ActionExpr read_action_expr(AnyVal val, MemHolder* h) {
        if (val.is_null() || !val.is_pointer()) return {};
        TinyMapView node(val, h);
        int32_t kind = read_int(node.get(uint8_t(ast::CODE)));
        ActionExpr e;
        e.kind = kind;
        if (kind == int32_t(ast::CAPTURE)) {
            e.index = read_int(node.get(uint8_t(ast::INDEX)));
        } else if (kind == int32_t(ast::INT_LIT) || kind == int32_t(ast::BOOL_LIT)) {
            e.int_val = read_int(node.get(uint8_t(ast::VALUE)));
        } else if (kind == int32_t(ast::STR_LIT)) {
            e.value = read_str(node.get(uint8_t(ast::VALUE)), h);
        }
        return e;
    }

    static std::optional<Action> read_action(AnyVal val, MemHolder* h) {
        if (val.is_null() || !val.is_pointer()) return std::nullopt;
        // Action is an ObjectMap: field_name → action_expr
        MapView action_map(val, h);
        Action a;
        action_map.ptr()->for_each([&](std::string_view key, AnyVal expr) {
            a.fields.push_back({
                std::string(key),
                read_action_expr(expr, h)
            });
        });
        return a;
    }

    static Item::Alt read_alt(AnyVal val, MemHolder* h) {
        Item::Alt alt;
        TinyMapView node(val, h);
        AnyVal seq_val    = node.get(uint8_t(ast::SEQ));
        AnyVal action_val = node.get(uint8_t(ast::ACTION));

        if (!seq_val.is_null()) {
            ArrayView seq(seq_val, h);
            for (uint64_t i = 0; i < seq.size(); ++i)
                alt.seq.push_back(read_item(seq.get(i), h));
        }
        alt.action = read_action(action_val, h);
        return alt;
    }

    static Item read_item(AnyVal val, MemHolder* h) {
        Item item;
        if (val.is_null() || !val.is_pointer()) return item;
        TinyMapView node(val, h);
        item.kind          = read_int(node.get(uint8_t(ast::CODE)));
        item.name          = read_str(node.get(uint8_t(ast::NAME)),    h);
        item.grammar_alias = read_str(node.get(uint8_t(ast::GRAMMAR)), h);
        item.min           = read_int(node.get(uint8_t(ast::MIN)));
        item.max           = read_int(node.get(uint8_t(ast::MAX)));

        // Sub-item (OPT, REP, LOOKAHEAD, NEG_AHEAD)
        AnyVal sub_val = node.get(uint8_t(ast::ITEM));
        if (!sub_val.is_null()) item.sub_items.push_back(read_item(sub_val, h));

        // Sub-alts (GROUP)
        AnyVal alts_val = node.get(uint8_t(ast::ALTS));
        if (!alts_val.is_null()) {
            ArrayView alts(alts_val, h);
            for (uint64_t i = 0; i < alts.size(); ++i)
                item.sub_alts.push_back(read_alt(alts.get(i), h));
        }
        return item;
    }

    static void read_rules(MapView& root, MemHolder* h, GrammarInfo& g) {
        AnyVal arr_val = root.get("rules");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem, h);
            Rule rule;
            rule.name = read_str(node.get(uint8_t(ast::NAME)), h);
            rule.group = read_str(node.get(uint8_t(ast::GROUP_NAME)), h);
            AnyVal alts_val = node.get(uint8_t(ast::ALTS));
            if (!alts_val.is_null()) {
                ArrayView alts(alts_val, h);
                for (uint64_t j = 0; j < alts.size(); ++j)
                    rule.alts.push_back(read_alt(alts.get(j), h));
            }
            g.rules.push_back(std::move(rule));
        }
    }

    static void read_imports(MapView& root, MemHolder* h,
                             const std::vector<ResolvedModule>& all_modules,
                             GrammarInfo& g) {
        AnyVal arr_val = root.get("imports");
        if (arr_val.is_null()) return;
        ArrayView arr(arr_val, h);
        for (uint64_t i = 0; i < arr.size(); ++i) {
            AnyVal elem = arr.get(i);
            if (elem.is_null()) continue;
            TinyMapView node(elem, h);
            std::string alias = read_str(node.get(uint8_t(ast::ALIAS)), h);
            std::string path  = read_str(node.get(uint8_t(ast::PATH)),  h);
            // Pull the imported module's OWN meta: output base name, namespace,
            // grammar name (→ Parser class) and whether it borrows its arena.
            ImportRef ref;
            ref.alias = alias;
            for (const auto& m : all_modules) {
                if (m.alias != alias) continue;
                if (m.grammar.root().is_null()) break;
                auto robj = m.grammar.root_object();
                if (robj.is_null()) break;
                auto rmap = robj.as_map();
                MemHolder* mh = m.grammar.holder();
                AnyVal meta_v = rmap.get("meta");
                if (!meta_v.is_null()) {
                    TinyMapView meta(meta_v, mh);
                    ref.output = read_str(meta.get(uint8_t(ast::OUTPUT)),    mh);
                    ref.ns     = read_str(meta.get(uint8_t(ast::NAMESPACE)), mh);
                    ref.name   = read_str(meta.get(uint8_t(ast::NAME)),      mh);
                }
                AnyVal ae = rmap.get("schema_arena_ext");
                ref.arena_ext = !ae.is_null() && ae.is_value() && ae.as_value<bool>();
                break;
            }
            if (ref.output.empty()) ref.output = alias;
            g.imports.push_back(std::move(ref));
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// CodeWriter — indented output stream helper
// ═══════════════════════════════════════════════════════════════════════════

class CodeWriter {
public:
    explicit CodeWriter(std::ostream& out) : out_(out) {}

    void line(std::string_view text = {}) {
        if (!text.empty()) {
            for (int i = 0; i < indent_; ++i) out_ << "    ";
        }
        out_ << text << '\n';
    }

    template <typename... Args>
    void fmt(std::format_string<Args...> f, Args&&... args) {
        line(std::format(f, std::forward<Args>(args)...));
    }

    void indent()   { ++indent_; }
    void dedent()   { if (indent_ > 0) --indent_; }

    struct Block {
        CodeWriter& w;
        ~Block() { w.dedent(); }
    };

    Block block(std::string_view open = "{") {
        line(open); indent();
        return Block{*this};
    }

private:
    std::ostream& out_;
    int           indent_ = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// CodeGen — GrammarInfo → C++ header + source
// ═══════════════════════════════════════════════════════════════════════════

class CodeGen {
public:
    CodeGen(const GrammarInfo& g, const fs::path& out_dir)
        : g_(g), out_dir_(out_dir)
        , parser_class_(to_pascal(g.name) + "Parser")
        , ast_ns_(g.name + "_ast") {}

    void emit_all() {
        emit_header();
        emit_source();
    }

    // The %fields/%nodes tables as a standalone constants header — the file the
    // compiler's Sema reads. Generating it is what makes the grammar the single
    // declaration of the AST's integers; the doc comments ride along so nothing
    // is lost in the move.
    //
    // Groups become nested namespaces, matching how the hand-written header
    // scoped `mod::` and `variant::` keys that deliberately reuse a global slot.
    void emit_ast_header(const fs::path& path, const std::string& grammar_path) {
        AtomicFile out(path);
        CodeWriter w(out.buf);
        const std::string ns = g_.cxx_namespace + "::ast";

        // Filename only: an absolute path would make the emitted file depend on
        // where the tree happens to live, and the staleness check diffs bytes.
        w.fmt("// Generated by peg_gen from {} — DO NOT EDIT.",
              fs::path(grammar_path).filename().string());
        w.line("//");
        w.line("// AST node codes (`Code`) and TinyObjectMap field keys (`Key`).");
        w.line("// Edit the grammar's %fields / %nodes blocks instead, then rerun");
        w.line("// the generator; the build checks this file is not stale.");
        w.line();
        w.line("#pragma once");
        w.line();
        w.line("#include <stdint.h>");
        w.line();
        w.line("#include <logos/core/named_code.hpp>");
        w.line();
        w.fmt("namespace {} {{", ns);
        w.line();
        w.line("using Key  = NamedCode<uint8_t>;");
        w.line("using Code = NamedCode<int32_t>;");
        w.line();

        auto emit_decl = [&](const char* kind, const NameDecl& d) {
            std::string decl = std::format("inline constexpr {} {}{{\"{}\", {}}};",
                                           kind, d.name, d.name, d.code);
            if (d.doc.empty()) w.line(decl);
            else               w.fmt("{}  // {}", decl, d.doc);
        };

        w.line("// ── Field keys (TinyObjectMap slots 0..51) ────────────────────────────────");
        for (const auto& f : g_.fields)
            if (f.group.empty()) emit_decl("Key", f);
        w.line();

        w.line("// ── Node type discriminants (the CODE field's value) ──────────────────────");
        for (const auto& n : g_.nodes) emit_decl("Code", n);

        // Grouped keys, in first-seen group order.
        std::vector<std::string> groups;
        for (const auto& f : g_.fields)
            if (!f.group.empty()
                && std::find(groups.begin(), groups.end(), f.group) == groups.end())
                groups.push_back(f.group);
        for (const auto& gname : groups) {
            w.line();
            w.fmt("namespace {} {{", gname);
            w.indent();
            for (const auto& f : g_.fields) if (f.group == gname) emit_decl("Key", f);
            w.dedent();
            w.fmt("}} // namespace {}", gname);
        }

        w.line();
        w.fmt("}} // namespace {}", ns);
    }

    // Called BEFORE any output file is opened. The C++ backend needs strictly
    // more from a %schema block than the Logos backend does, because it has no
    // second compile pass in which field names and type codes get resolved
    // against the real ADR-0011 `schema` item:
    //   • an explicit `= KEY` per field  (Logos emits `node.f = …`; logosc maps
    //     the field name to its key when compiling the generated source);
    //   • the node's `code(...)` type code (Logos gets it from `doc.make::<S>()`).
    // Both live in the .logos schema item today (stdlib/std/wql/{ir,plan}.logos).
    // Carrying them in the %schema block is what makes the .peg the single
    // source of truth for BOTH backends — and lets the generators emit the
    // C++ constants and the Logos `schema` items instead of hand-mirroring them.
    // Structural validation of a %schema block, run BEFORE any output file is
    // opened. Everything checkable without an action context lives here so the
    // common authoring mistakes are reported together, not one per re-run.
    // Every %skip pattern must map to a matcher we actually emit. Runs before
    // any output file is opened.
    static void validate_skips(const GrammarInfo& g, const std::string& path) {
        std::string errs;
        for (const auto& t : g.tokens) {
            if (t.kind != int32_t(ast::TOKEN_SKIP)) continue;
            if (skip_pattern_recognised(regex_inner(t.pattern))) continue;
            errs += std::format("  {}\n", t.pattern);
        }
        if (errs.empty()) return;
        std::fprintf(stderr,
            "peg_gen: %s: unrecognised %%skip pattern(s):\n%s"
            "There is no regex engine: a pattern selects one of three hand-written\n"
            "matchers — a literal character class `[…]+`, a `//` line comment, or a\n"
            "`/*…*/` block comment. Anything else would silently emit NO matcher.\n",
            path.c_str(), errs.c_str());
        std::exit(1);
    }

    static void validate_schema(const GrammarInfo& g, const std::string& path) {
        std::string errs;
        for (const auto& s : g.schemas) {
            if (!s.has_type_code)
                errs += std::format("  {}: missing `code(0x…)` type code\n", s.name);
            if (!s.has_cap || s.cap <= 0)
                errs += std::format("  {}: missing `cap(N)` — the slot capacity of "
                                    "the mirrored `schema` item\n", s.name);
            for (const auto& f : s.fields) {
                if (!ftype_is_known(f))
                    errs += std::format("  {}.{}: unknown field type \"{}\"\n",
                                        s.name, f.name, f.ftype);
                if (f.is_fan()) {
                    if (f.fan_cap() <= 0)
                        errs += std::format("  {}.{}: fan needs a trailing slot "
                                            "count: \"fan <setter> <maxfn> <cap>\"\n",
                                            s.name, f.name);
                    if (!f.has_key)
                        errs += std::format("  {}.{}: fan needs `= <first slot key>`\n",
                                            s.name, f.name);
                    const SchemaField* len = s.find("count");
                    if (!len || !len->has_key)
                        errs += std::format("  {}.{}: fan node must declare "
                                            "`count: \"i32\" = <key>`\n", s.name, f.name);
                } else if (!f.has_key) {
                    errs += std::format("  {}.{}: missing `= KEY`\n", s.name, f.name);
                }
            }
        }
        if (errs.empty()) return;
        std::fprintf(stderr,
            "peg_gen: %s: the C++ backend needs the %%schema block to mirror its\n"
            "ADR-0011 `schema` item exactly — `S : code(0x…) { f: \"ty\" = KEY }`.\n"
            "Unlike the Logos backend there is no second pass to resolve them.\n%s",
            path.c_str(), errs.c_str());
        std::exit(1);
    }

private:
    const GrammarInfo& g_;
    fs::path           out_dir_;
    std::string        parser_class_;
    std::string        ast_ns_;
    int                lc_ = 0;   // label counter — reset per rule, always increasing
    std::string        rcap_var_;       // name of the rule-captures array for $... in current alt
    std::string        cur_rule_group_; // current rule's group tag (empty = none)
    std::string        cur_fold_var_;   // name of the fold accumulator variable (for $0)
    std::string        fold_init_cap_;  // cap name to initialise the next fold REP from

    // Returns a unique label suffix string within the current rule.
    std::string fresh() { return std::to_string(lc_++); }

    // Returns true if any item tree references the GT_TYPE pseudo-token.
    static bool items_use_gt_type(const std::vector<Item>& items) {
        for (const auto& item : items) {
            if (item.kind == int32_t(ast::TOKEN_REF) && item.name == "GT_TYPE")
                return true;
            if (items_use_gt_type(item.sub_items)) return true;
            for (const auto& sa : item.sub_alts)
                if (items_use_gt_type(sa.seq)) return true;
        }
        return false;
    }

    // Returns true if any rule in the grammar references the GT_TYPE pseudo-token.
    static bool grammar_uses_gt_type(const GrammarInfo& g) {
        for (const auto& rule : g.rules)
            for (const auto& alt : rule.alts)
                if (items_use_gt_type(alt.seq)) return true;
        return false;
    }

    // Returns true if any item tree references the LT_TYPE pseudo-token.
    static bool items_use_lt_type(const std::vector<Item>& items) {
        for (const auto& item : items) {
            if (item.kind == int32_t(ast::TOKEN_REF) && item.name == "LT_TYPE")
                return true;
            if (items_use_lt_type(item.sub_items)) return true;
            for (const auto& sa : item.sub_alts)
                if (items_use_lt_type(sa.seq)) return true;
        }
        return false;
    }

    // Returns true if any rule in the grammar references the LT_TYPE pseudo-token.
    static bool grammar_uses_lt_type(const GrammarInfo& g) {
        for (const auto& rule : g.rules)
            for (const auto& alt : rule.alts)
                if (items_use_lt_type(alt.seq)) return true;
        return false;
    }

    // Raw-group pseudo-tokens: RAW_GROUP_PAREN / RAW_GROUP_BRACKET /
    // RAW_GROUP_BRACE consume a balanced delimiter pair as raw source
    // text. Used by function-style macros (`name!(...)`) so the
    // captured tokens can be re-interpreted per callee marker
    // (#[fn_macro] re-parses as expr-list; #[token_macro] lexes as
    // TokenStream). The pseudo-token "returns" a captured string view.
    static bool is_raw_group_token(std::string_view name) {
        return name == "RAW_GROUP_PAREN"
            || name == "RAW_GROUP_BRACKET"
            || name == "RAW_GROUP_BRACE";
    }
    static bool items_use_raw_group(const std::vector<Item>& items) {
        for (const auto& item : items) {
            if (item.kind == int32_t(ast::TOKEN_REF)
                && is_raw_group_token(item.name)) return true;
            if (items_use_raw_group(item.sub_items)) return true;
            for (const auto& sa : item.sub_alts)
                if (items_use_raw_group(sa.seq)) return true;
        }
        return false;
    }
    static bool grammar_uses_raw_group(const GrammarInfo& g) {
        for (const auto& rule : g.rules)
            for (const auto& alt : rule.alts)
                if (items_use_raw_group(alt.seq)) return true;
        return false;
    }

    static bool action_has_array_capture(const Action& action) {
        for (const auto& f : action.fields)
            if (f.expr.kind == int32_t(ast::ARRAY_CAPTURE)) return true;
        return false;
    }

    static bool action_has_fold_capture(const Action& action) {
        for (const auto& f : action.fields)
            if (f.expr.kind == int32_t(ast::FOLD_CAPTURE)) return true;
        return false;
    }

    // Rules that carry a packrat memoisation cache.  Each entry is keyed
    // by the start position and stores (result AST, end position).
    //
    // Only safe because peg_gen's emitted code no longer rolls back the
    // doc arena on backtrack — AST nodes allocated during failed branches
    // sit in the arena as unreachable garbage rather than being zeroed
    // and reused, so pointers stashed in the memo stay valid.
    //
    // Memoisation pays off for rules that (a) are called at the same pos
    // from multiple alternative contexts, and (b) do real recursive work.
    static bool is_memoized(std::string_view rule) {
        static constexpr std::string_view kMemo[] = {
            "expr", "log_expr", "cmp_expr", "bitwise_expr",
            "add_expr", "mul_expr", "cast_expr", "unary_expr",
            "atom", "primary_expr",
            "type_ref", "simple_type",
        };
        for (auto r : kMemo) if (r == rule) return true;
        return false;
    }

    // A "token-alias" rule is one whose every alternative is a single
    // TOKEN_REF and carries no user action — i.e. a disjunction of tokens
    // like `trait_kw <- KW_TRAIT / KW_GENOS`.  Callers that reference such
    // a rule want "match any of these tokens" semantics and should NOT
    // collect the result into the per-alt `$...` rcap array (otherwise the
    // token text pollutes the parent's ITEMS list).  Treated exactly like
    // a token from the caller's viewpoint.
    bool is_token_alias(std::string_view rule_name) const {
        for (const auto& r : g_.rules) {
            if (r.name != rule_name) continue;
            for (const auto& a : r.alts) {
                if (a.action.has_value()) return false;
                if (a.seq.size() != 1) return false;
                if (a.seq[0].kind != int32_t(ast::TOKEN_REF)) return false;
            }
            return !r.alts.empty();
        }
        return false;
    }

    // A REP is in fold-mode when its body GROUP has at least one alt with $0 ($FOLD_CAPTURE).
    static bool rep_is_fold(const Item& rep) {
        if (rep.sub_items.empty()) return false;
        const auto& body = rep.sub_items[0];
        if (body.kind == int32_t(ast::GROUP)) {
            for (const auto& sa : body.sub_alts)
                if (sa.action && action_has_fold_capture(*sa.action)) return true;
        }
        return false;
    }

    // ── Header ──────────────────────────────────────────────────────────────

    // Emission is ATOMIC: build the whole file in memory, write it once at the
    // end. The generator can bail mid-emission on a grammar error (see
    // schema_error), and a truncated .hpp/.cpp left on disk would be picked up
    // by a later build.
    struct AtomicFile {
        std::ostringstream buf;
        fs::path           path;
        explicit AtomicFile(fs::path p) : path(std::move(p)) {}
        ~AtomicFile() {
            if (std::uncaught_exceptions()) return;
            std::ofstream f(path);
            f << buf.str();
        }
    };

    void emit_header() {
        AtomicFile out(out_dir_ / (g_.output + ".hpp"));
        CodeWriter w(out.buf);

        w.line("// Generated by peg_gen — DO NOT EDIT.");
        w.line();
        w.fmt("#pragma once");
        w.line();
        w.line("#include <string_view>");
        w.line("#include <unordered_map>");
        w.line("#include <vector>");
        w.line("#include <utility>");
        if (g_.schema_mode()) w.line("#include <charconv>");   // %schema value decoders
        w.line("#include <logos/core/named_code.hpp>");
        w.line("#include <logos/writ/compat.hpp>");
        for (const auto& imp : g_.imports)
            w.fmt("#include \"{}.hpp\"", imp.output);
        w.line();

        // Open namespace.
        w.fmt("namespace {} {{", g_.cxx_namespace);
        w.line();

        // AST constants namespace.
        w.fmt("// ── AST field keys (%fields) and node codes (%nodes) ─────────────────────");
        w.fmt("namespace {} {{", ast_ns_);
        w.indent();
        w.line("using Key  = logos::NamedCode<uint8_t>;");
        w.line("using Code = logos::NamedCode<int32_t>;");
        w.line();

        if (!g_.fields.empty()) {
            w.line("// Field keys (TinyObjectMap slot indices).  Group-scoped keys");
            w.line("// live in nested namespaces; the same slot number may be reused");
            w.line("// across groups because distinct node types never co-exist in one map.");
            // Emit global first.
            for (const auto& f : g_.fields)
                if (f.group.empty())
                    w.fmt("inline constexpr Key  {:20s} {{\"{}\", {}}};", f.name, f.name, f.code);
            // Emit each group.  Collect groups preserving declaration order.
            std::vector<std::string> groups;
            for (const auto& f : g_.fields) {
                if (f.group.empty()) continue;
                if (std::find(groups.begin(), groups.end(), f.group) == groups.end())
                    groups.push_back(f.group);
            }
            for (const auto& g : groups) {
                w.line();
                w.fmt("namespace {} {{", g);
                w.indent();
                for (const auto& f : g_.fields)
                    if (f.group == g)
                        w.fmt("inline constexpr Key  {:20s} {{\"{}\", {}}};", f.name, f.name, f.code);
                w.dedent();
                w.fmt("}} // namespace {}", g);
            }
            w.line();
        }
        if (!g_.nodes.empty()) {
            w.line("// Node type discriminants");
            for (const auto& n : g_.nodes)
                w.fmt("inline constexpr Code {:20s} {{\"{}\", {}}};", n.name, n.name, n.code);
            w.line();
        }
        w.dedent();
        w.fmt("}} // namespace {}", ast_ns_);
        w.line();

        // Token enum (generated from %tokens).
        if (!g_.tokens.empty()) {
            w.line("// ── Token kinds (generated from %tokens) ────────────────────────────────");
            int32_t skip_code = int32_t(ast::TOKEN_SKIP);
            w.fmt("enum class TK_{} : int {{", to_upper(g_.name));
            w.indent();
            w.line("Eof = 0,");
            w.line("Invalid,");
            for (const auto& t : g_.tokens) {
                if (t.kind == skip_code) continue; // skips are not tokens
                w.fmt("{},   // {}", safe_tok_name(t.name), t.pattern);
            }
            w.dedent();
            w.line("};");
            w.line();
        }

        // Parser class.
        w.fmt("// ── Parser ───────────────────────────────────────────────────────────────");
        w.fmt("class {} {{", parser_class_);
        w.line("public:");
        w.indent();

        // `arena: external` — the caller owns the Writ and the parser borrows it,
        // so an imported grammar's sub-parse composes straight into the outer
        // document. Otherwise the parser allocates and hands out its own.
        if (g_.arena_ext)
            w.fmt("{}(std::string_view source, logos::writ::WritCtr& doc);", parser_class_);
        else
            w.fmt("explicit {}(std::string_view source);", parser_class_);
        w.line();

        if (!g_.exports.empty()) {
            w.line("// Entry points for exported rules.");
            for (const auto& e : g_.exports)
                if (g_.arena_ext)
                    w.fmt("logos::writ::AnyVal parse_{}();", e);
                else
                    w.fmt("logos::writ::Writ parse_{}();", e);
            w.line();
        }

        if (!g_.tokens.empty()) {
            w.line("// Returns true when all input tokens have been consumed.");
            w.line("bool at_eof() { return peek_token().kind == TK::Eof; }");
            w.line("// Line number of the next unconsumed token (1-based).");
            w.line("uint32_t next_line() { return peek_token().line; }");
            w.line("// Text of the next unconsumed token.");
            w.line("std::string_view next_text() { return peek_token().text; }");
            w.line("// Furthest token the parser ever successfully consumed.");
            w.line("// Points to the deepest parse position before backtracking —");
            w.line("// use this for error reporting instead of next_text()/next_line().");
            w.line("uint32_t         furthest_line() const { return furthest_.line ? furthest_.line : 1; }");
            w.line("std::string_view furthest_text() const { return furthest_.text; }");
            // 1-based column of furthest_ within source_. Walks back from
            // the token's text pointer to the prior newline. Returns 0 if
            // the token isn't in source_ (defensive — shouldn't happen).
            w.line("uint32_t furthest_column() const {");
            w.line("    if (furthest_.text.data() < source_.data() ||");
            w.line("        furthest_.text.data() > source_.data() + source_.size()) return 0;");
            w.line("    size_t off = furthest_.text.data() - source_.data();");
            w.line("    size_t i = off; while (i > 0 && source_[i - 1] != '\\n') --i;");
            w.line("    return static_cast<uint32_t>(off - i + 1);");
            w.line("}");
            // Same for `next_text()` (the unconsumed peek).
            w.line("uint32_t next_column() {");
            w.line("    auto t = peek_token();");
            w.line("    if (t.text.data() < source_.data() ||");
            w.line("        t.text.data() > source_.data() + source_.size()) return 0;");
            w.line("    size_t off = t.text.data() - source_.data();");
            w.line("    size_t i = off; while (i > 0 && source_[i - 1] != '\\n') --i;");
            w.line("    return static_cast<uint32_t>(off - i + 1);");
            w.line("}");
            w.line();
        }

        w.dedent();
        w.line("private:");
        w.indent();

        // Rule method declarations.
        for (const auto& r : g_.rules) {
            w.fmt("logos::writ::AnyVal rule_{}();", r.name);
            if (is_memoized(r.name))
                w.fmt("logos::writ::AnyVal rule_{}_impl();", r.name);
        }
        // Packrat memo caches: start_pos → (AST, end_pos).  AST AnyVals
        // remain valid because emit_alt no longer rolls back the arena.
        //
        // Indexed by byte offset (the key is `pos_`, dense in 0..source.size())
        // rather than hashed: a flat vector sized once in the ctor kills the
        // per-insert node malloc + rehash churn that an unordered_map incurs on
        // the expr-precedence ladder (every atom is memoized through ~10 layers).
        // `.second == kMemoEmpty` marks an unfilled slot — parse FAILURES are
        // memoized too (null AnyVal is a valid result), so the end-pos sentinel,
        // not the value, signals emptiness.
        if (std::any_of(g_.rules.begin(), g_.rules.end(),
                        [&](const auto& r){ return is_memoized(r.name); })) {
            w.line("static constexpr size_t kMemoEmpty = static_cast<size_t>(-1);");
            // Memo cell carries the END line as well as the end position: a cache
            // HIT jumps pos_ to `end` and so must also restore line_ to the line
            // AT that end, or every newline the cached parse spanned goes
            // uncounted (line_ undercounts → SRC_LINE skews for everything after
            // a memoized multi-line rule, e.g. a `match`). `.end == kMemoEmpty`
            // still marks an unfilled slot (failures are memoized too).
            w.line("struct MemoCell { logos::writ::AnyVal first; size_t end; uint32_t line; };");
        }
        for (const auto& r : g_.rules) {
            if (!is_memoized(r.name)) continue;
            w.fmt("std::vector<MemoCell> memo_{}_;", r.name);
        }
        if (!g_.prec.empty()) {
            w.line();
            w.line("// Pratt expression parser (generated from %prec).");
            w.line("logos::writ::AnyVal pratt_expr(int min_prec = 0);");
            w.line("logos::writ::AnyVal pratt_atom();");
        }

        w.line();
        w.line("// Lexer state.");
        if (!g_.tokens.empty()) {
            w.fmt("using TK = TK_{};", to_upper(g_.name));
            w.line("struct Token { TK kind; std::string_view text; uint32_t line = 0; };");
            if (g_.schema_mode()) {
                // %schema value decoders. A `WAny`/scalar field encodes the RAW
                // TEXT of the token that produced it, so the generated parser
                // needs these three. Counterparts of the Logos backend's
                // wstr_decode_i64 / wstr_decode_f64 / wstr_unquote.
                // Mirrors wstr_decode_i64 EXACTLY, including its error policy: a
                // literal above i64::MAX yields the poison sentinel i64::MIN, which
                // a later check phase turns into "integer literal out of range".
                // std::from_chars cannot serve — on overflow it leaves the output
                // untouched, so `99999999999999999999` would decode to 0 and the
                // query would compile with a silently wrong constant.
                w.line("static inline int64_t peg_decode_i64(std::string_view s) {");
                w.line("    int64_t v = 0;");
                w.line("    for (char c : s) {");
                w.line("        if (c < '0' || c > '9') break;   // stops at a type suffix");
                w.line("        int64_t d = c - '0';");
                w.line("        if (v > 922337203685477580LL || (v == 922337203685477580LL && d > 7))");
                w.line("            return INT64_MIN;            // el_lit_poison()");
                w.line("        v = v * 10 + d;");
                w.line("    }");
                w.line("    return v;");
                w.line("}");
                w.line("static inline double peg_decode_f64(std::string_view s) {");
                w.line("    double v = 0; std::from_chars(s.data(), s.data() + s.size(), v); return v;");
                w.line("}");
                w.line("// An `INTEGER?` capture: interned token text, or null → 0.");
                w.line("static inline int64_t peg_decode_i64_any(logos::writ::AnyVal v,");
                w.line("                                         logos::writ::WritCtr& d) {");
                w.line("    if (v.is_null()) return 0;");
                w.line("    return peg_decode_i64(logos::writ::StringView(v, d.holder()).view());");
                w.line("}");
                w.line("// Strip one layer of surrounding quotes (the token text includes them).");
                w.line("static inline std::string_view peg_unquote(std::string_view s) {");
                w.line("    if (s.size() >= 2 && (s.front() == '\"' || s.front() == '\\'')");
                w.line("        && s.back() == s.front()) return s.substr(1, s.size() - 2);");
                w.line("    return s;");
                w.line("}");
            }
            // Pos-indexed token cache cell: the scanned token plus the (pos,line)
            // it ends at. Lexing is a pure function of (pos, source), so caching
            // by entry pos lets every backtracking re-visit skip the re-scan.
            w.line("struct TokCell { Token tok; size_t endpos; uint32_t endline; };");
            w.line("Token lex_one();");
            w.line("Token lex_one_raw();");
            w.line("Token next_token();");
            w.line("Token peek_token();");
            w.line("bool  try_token(TK kind);");
            w.line("Token expect_token(TK kind, std::string_view what);");
            if (grammar_uses_gt_type(g_))
                w.line("bool  try_token_gt(); // GT_TYPE pseudo-token: also splits SHR (>>) into two GTs");
            if (grammar_uses_lt_type(g_))
                w.line("bool  try_token_lt(); // LT_TYPE pseudo-token: also splits SHL (<<) into two LTs");
            if (grammar_uses_raw_group(g_)) {
                w.line("bool  try_raw_group_paren(std::string_view& out_text);");
                w.line("bool  try_raw_group_bracket(std::string_view& out_text);");
                w.line("bool  try_raw_group_brace(std::string_view& out_text);");
            }
        }

        w.line();
        if (g_.arena_ext) w.line("logos::writ::WritCtr& doc_;   // borrowed (arena: external)");
        else              w.line("logos::writ::Writ doc_;");
        w.line("std::string_view         source_;");
        w.line("size_t                   pos_ = 0;");
        w.line("uint32_t                 line_ = 1;  // current source line (1-based)");
        if (!g_.tokens.empty()) {
            w.line("Token                    la_{};");
            w.line("bool                     have_la_ = false;");
            w.line("Token                    furthest_{}; // furthest successfully consumed token");
            // Flat pos-indexed token cache (sized source.size()+1 in the ctor).
            // endpos == kTokNotLexed marks an unscanned slot. Backtrack-stable:
            // reset() never clears it, so re-visited positions hit the cache.
            w.line("static constexpr size_t kTokNotLexed = static_cast<size_t>(-1);");
            w.line("std::vector<TokCell>     tbuf_;");
        }

        // NOTE: no sub-parser member fields. An embedded grammar is parsed by a
        // STACK-LOCAL parser constructed over a raw sub-range of the source and
        // sharing this parser's Writ — see emit_embed_call. The old member-field
        // scheme assumed a nested parser over the SAME token stream, could not be
        // initialised (the generated Parser has no default ctor), and qualified
        // the class with the importing grammar's namespace.

        w.dedent();
        w.fmt("}}; // class {}", parser_class_);
        w.line();
        w.fmt("}} // namespace {}", g_.cxx_namespace);
        w.line();
    }

    // ── Source ───────────────────────────────────────────────────────────────

    void emit_source() {
        AtomicFile out(out_dir_ / (g_.output + ".cpp"));
        CodeWriter w(out.buf);

        w.line("// Generated by peg_gen — DO NOT EDIT.");
        w.line();
        w.fmt("#include \"{}.hpp\"", g_.output);
        w.line("#include <logos/writ/schema_codes.hpp>");
        // (schema_codes folded above)
        w.line("#include <logos/verification/assert.hpp>");
        w.line("#include <charconv>");
        w.line("#include <cctype>");
        w.line("#include <cstdio>");
        w.line();
        w.fmt("namespace {} {{", g_.cxx_namespace);
        w.line();
        w.fmt("using namespace {};", ast_ns_);
        w.line("using logos::writ::AnyVal;");
        // Bring TK into namespace scope so out-of-class method definitions can reference it.
        if (!g_.tokens.empty())
            w.fmt("using TK = TK_{};", to_upper(g_.name));
        w.line();

        emit_lexer(w);
        emit_embed_runtime(w);
        emit_public_entries(w);
        emit_rules(w);
        if (!g_.prec.empty()) emit_pratt(w);

        w.fmt("}} // namespace {}", g_.cxx_namespace);
        w.line();
    }

    // ── Lexer ────────────────────────────────────────────────────────────────

    void emit_lexer(CodeWriter& w) {
        if (g_.tokens.empty()) return;

        int32_t lit_code  = int32_t(ast::TOKEN_LITERAL);
        int32_t skip_code = int32_t(ast::TOKEN_SKIP);

        w.line("// ── Lexer ─────────────────────────────────────────────────────────────────");
        w.line();

        // next / peek / try / expect
        // Size the packrat memo vectors once: keys are byte offsets in
        // 0..source.size(), so one slot per offset covers every possible start.
        if (g_.arena_ext)
            w.fmt("{0}::{0}(std::string_view source, logos::writ::WritCtr& doc)"
                  " : doc_(doc), source_(source) {{", parser_class_);
        else
            w.fmt("{0}::{0}(std::string_view source) : source_(source) {{", parser_class_);
        w.indent();
        for (const auto& r : g_.rules) {
            if (!is_memoized(r.name)) continue;
            w.fmt("memo_{}_.assign(source.size() + 1, {{logos::writ::AnyVal{{}}, kMemoEmpty, 0}});",
                  r.name);
        }
        // Token cache: one slot per byte offset, all initially unscanned.
        w.line("tbuf_.assign(source.size() + 1, TokCell{Token{}, kTokNotLexed, 0});");
        w.dedent();
        w.line("}");
        w.line();
        w.fmt("{0}::Token {0}::next_token() {{", parser_class_);
        w.indent();
        w.line("Token t;");
        w.line("if (have_la_) { have_la_ = false; t = la_; } else { t = lex_one(); }");
        w.line("// Track furthest consumed position for error reporting.");
        w.line("if (t.kind != TK::Eof &&");
        w.line("    (t.line > furthest_.line ||");
        w.line("     (t.line == furthest_.line && t.text.data() >= furthest_.text.data())))");
        w.line("    furthest_ = t;");
        w.line("return t;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("{0}::Token {0}::peek_token() {{", parser_class_);
        w.indent();
        w.line("if (!have_la_) { la_ = lex_one(); have_la_ = true; }");
        w.line("return la_;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("bool {0}::try_token(TK kind) {{", parser_class_);
        w.indent();
        w.line("Token t = peek_token();");
        w.line("if (t.kind == kind) {");
        w.indent();
        w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
        w.line("    furthest_ = t;");
        w.line("next_token(); return true;");
        w.dedent();
        w.line("}");
        w.line("return false;");
        w.dedent();
        w.line("}");
        w.line();

        // GT_TYPE pseudo-token: accepts TK::GT or splits TK::SHR (>>) into GT + pushed-back GT.
        // Only emitted for grammars that actually use the GT_TYPE pseudo-token.
        if (grammar_uses_gt_type(g_)) {
            w.fmt("bool {0}::try_token_gt() {{", parser_class_);
            w.indent();
            w.line("Token t = peek_token();");
            w.line("if (t.kind == TK::GT) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token(); return true;");
            w.dedent();
            w.line("}");
            w.line("if (t.kind == TK::SHR) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token();");
            w.line("la_ = Token{TK::GT, t.text.substr(1, 1), t.line};");
            w.line("have_la_ = true;");
            w.line("return true;");
            w.dedent();
            w.line("}");
            w.line("return false;");
            w.dedent();
            w.line("}");
            w.line();
        }

        // LT_TYPE pseudo-token: accepts TK::LT or splits TK::SHL (<<) into LT + pushed-back LT.
        // Mirror of try_token_gt; lets type-position contexts admit `<<` as
        // two opening brackets (e.g. `Foo<<type:CFG.key>>`).
        if (grammar_uses_lt_type(g_)) {
            w.fmt("bool {0}::try_token_lt() {{", parser_class_);
            w.indent();
            w.line("Token t = peek_token();");
            w.line("if (t.kind == TK::LT) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token(); return true;");
            w.dedent();
            w.line("}");
            w.line("if (t.kind == TK::SHL) {");
            w.indent();
            w.line("if (t.line > furthest_.line || (t.line == furthest_.line && t.text.data() >= furthest_.text.data()))");
            w.line("    furthest_ = t;");
            w.line("next_token();");
            w.line("la_ = Token{TK::LT, t.text.substr(1, 1), t.line};");
            w.line("have_la_ = true;");
            w.line("return true;");
            w.dedent();
            w.line("}");
            w.line("return false;");
            w.dedent();
            w.line("}");
            w.line();
        }

        // Raw-group pseudo-tokens. Each variant consumes its opening
        // delimiter, then walks tokens with a nested-delim stack until
        // the matching outer close. Captures raw source bytes (incl.
        // whitespace/comments) between the delims as a std::string_view
        // into source_. On mismatch / EOF inside the body, restores
        // parser state so the alt's fail-label can back-track cleanly.
        if (grammar_uses_raw_group(g_)) {
            auto emit_raw_group = [&](const char* fn_name,
                                      const char* open_tok,
                                      const char* close_tok) {
                w.fmt("bool {0}::{1}(std::string_view& out_text) {{",
                      parser_class_, fn_name);
                w.indent();
                w.line("size_t save_pos = pos_; bool save_la = have_la_;");
                w.line("Token save_tok = la_; uint32_t save_line = line_;");
                w.fmt("if (peek_token().kind != TK::{}) return false;", open_tok);
                w.line("next_token();");
                w.line("size_t start = pos_;  // first byte past the open delim");
                w.line("std::vector<TK> stack;");
                w.fmt("stack.push_back(TK::{});", close_tok);
                w.line("while (!stack.empty()) {");
                w.indent();
                w.line("Token t = peek_token();");
                w.line("if (t.kind == TK::Eof) {");
                w.indent();
                w.line("// Unbalanced — restore and reject.");
                w.line("pos_ = save_pos; have_la_ = save_la; la_ = save_tok; line_ = save_line;");
                w.line("return false;");
                w.dedent();
                w.line("}");
                // ── Byte-level raw-body robustness (Phase 3) ─────────────────
                // A backslash-escape or a backtick-delimited region lets literal
                // template text carry a STRAY brace WITHOUT breaking balance.
                // Both a bare `\` and a bare backtick lex as one-char Invalid
                // tokens, so we intercept them here and resync the lexer at the
                // byte level. This is strictly additive: well-formed jinja never
                // relies on either, so the default balanced-brace walk (and its
                // byte-identical capture) is unchanged for existing macros.
                w.line("if (t.kind == TK::Invalid && t.text.size() == 1) {");
                w.indent();
                w.line("size_t bpos = static_cast<size_t>(t.text.data() - source_.data());");
                w.line("char ic = t.text[0];");
                // (a) Backslash escape: `\{`, `\}`, `` \` ``, `\\`. The backslash
                // is consumed; the escaped char passes through into the captured
                // body verbatim and does NOT count toward brace balance. The
                // handler therefore sees the char WITHOUT the leading backslash.
                w.line("if (ic == '\\\\' && bpos + 1 < source_.size()) {");
                w.indent();
                w.line("char nx = source_[bpos + 1];");
                w.line("if (nx == '{' || nx == '}' || nx == '`' || nx == '\\\\') {");
                w.indent();
                w.line("if (nx == '\\n') ++line_;");
                w.line("pos_ = bpos + 2; have_la_ = false; continue;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                // (b) Backtick-delimited region: raw bytes up to the matching
                // backtick are passed through with NO brace counting inside.
                // Resync the lexer just past the closing backtick (counting any
                // embedded newlines so line_ stays accurate). An unterminated
                // backtick falls through to the ordinary Invalid-token path.
                w.line("if (ic == '`') {");
                w.indent();
                w.line("size_t j = bpos + 1; uint32_t nl = 0;");
                w.line("while (j < source_.size() && source_[j] != '`') { if (source_[j] == '\\n') ++nl; ++j; }");
                w.line("if (j < source_.size()) {");
                w.indent();
                w.line("line_ += nl; pos_ = j + 1; have_la_ = false; continue;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.line("switch (t.kind) {");
                w.line("case TK::LPAREN:   stack.push_back(TK::RPAREN);   break;");
                w.line("case TK::LBRACKET: stack.push_back(TK::RBRACKET); break;");
                w.line("case TK::LBRACE:   stack.push_back(TK::RBRACE);   break;");
                w.line("case TK::RPAREN:");
                w.line("case TK::RBRACKET:");
                w.line("case TK::RBRACE: {");
                w.indent();
                w.line("if (stack.back() != t.kind) {");
                w.indent();
                w.line("pos_ = save_pos; have_la_ = save_la; la_ = save_tok; line_ = save_line;");
                w.line("return false;");
                w.dedent();
                w.line("}");
                w.line("stack.pop_back();");
                w.line("if (stack.empty()) {");
                w.indent();
                w.line("size_t end = static_cast<size_t>(t.text.data() - source_.data());");
                w.line("out_text = source_.substr(start, end - start);");
                w.line("next_token();");
                w.line("return true;");
                w.dedent();
                w.line("}");
                w.line("break;");
                w.dedent();
                w.line("}");
                w.line("default: break;");
                w.line("}");
                w.line("next_token();");
                w.dedent();
                w.line("}");
                w.line("return false;  // unreachable");
                w.dedent();
                w.line("}");
                w.line();
            };
            emit_raw_group("try_raw_group_paren",   "LPAREN",   "RPAREN");
            emit_raw_group("try_raw_group_bracket", "LBRACKET", "RBRACKET");
            emit_raw_group("try_raw_group_brace",   "LBRACE",   "RBRACE");
        }

        w.fmt("{0}::Token {0}::expect_token(TK kind, std::string_view what) {{", parser_class_);
        w.indent();
        w.line("Token t = next_token();");
        w.fmt("LOGOS_ASSERT(t.kind == kind, \"{}-LEX-001\",", to_upper(g_.name));
        w.line("    \"expected {}, got '{}'\", what, t.text);");
        w.line("return t;");
        w.dedent();
        w.line("}");
        w.line();

        // lex_one — pos-indexed token cache wrapping the raw scanner. A hit
        // jumps pos_/line_ to the cached end and returns the token without
        // re-scanning (the dominant win under packrat backtracking).
        w.fmt("{0}::Token {0}::lex_one() {{", parser_class_);
        w.indent();
        w.line("size_t k = pos_;");
        w.line("const TokCell& c = tbuf_[k];");
        w.line("if (c.endpos != kTokNotLexed) { pos_ = c.endpos; line_ = c.endline; return c.tok; }");
        w.line("Token t = lex_one_raw();");
        w.line("tbuf_[k] = TokCell{t, pos_, line_};");
        w.line("return t;");
        w.dedent();
        w.line("}");
        w.line();

        // lex_one_raw — the actual scanner.
        w.fmt("{0}::Token {0}::lex_one_raw() {{", parser_class_);
        w.indent();

        // Emit skip patterns first.
        w.line("// Skip whitespace and comments.");
        w.line("while (pos_ < source_.size()) {");
        w.indent();
        w.line("char c = source_[pos_];");
        // Whitespace skip. This USED to be an exact string compare against
        // `/[ \t\n\r]+/`, so a grammar that spelled the same class in another
        // order (el.peg writes `[ \t\r\n]+`) silently got a parser that could
        // not skip whitespace at all. Parse the class instead.
        std::string ws;
        for (const auto& t : g_.tokens) {
            if (t.kind != skip_code) continue;
            ws = skip_char_class(regex_inner(t.pattern));
            if (!ws.empty()) break;
        }
        if (!ws.empty()) {
            std::string cond;
            for (char ch : ws) {
                if (!cond.empty()) cond += " || ";
                cond += std::format("c == '{}'", char_lit(ch));
            }
            const bool counts_lines = ws.find('\n') != std::string::npos;
            w.fmt("if ({}) {{ {}++pos_; continue; }}", cond,
                  counts_lines ? "if (c == '\\n') ++line_; " : "");
        }
        // Line comment skip: pattern inner starts with // or \/\/ (escaped-slash pair).
        // If the grammar also defines a DOC_LINE regex token (matching `///[^\n]*`),
        // restrict the skip so `///` and `//!` are passed through to the token matcher
        // below. Four-slash `////` and beyond stay as ordinary comments.
        bool has_doc_line = false;
        for (const auto& t : g_.tokens) {
            if (t.kind == int32_t(ast::TOKEN_REGEX) && t.name == "DOC_LINE") {
                has_doc_line = true; break;
            }
        }
        for (const auto& t : g_.tokens) {
            if (t.kind != skip_code) continue;
            auto inner = regex_inner(t.pattern);
            if (inner.starts_with("//") || inner.starts_with("\\/\\/")) {
                if (has_doc_line) {
                    // Skip `//` line-comments EXCEPT:
                    //   - outer-doc `///` (followed by non-`/`)
                    //   - inner-doc `//!`
                    // `////+` is a normal comment again per Rust rules.
                    w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '/' &&");
                    w.line("    !(pos_+2 < source_.size() && source_[pos_+2] == '/' &&");
                    w.line("      !(pos_+3 < source_.size() && source_[pos_+3] == '/')) &&");
                    w.line("    !(pos_+2 < source_.size() && source_[pos_+2] == '!')) {");
                } else {
                    w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '/') {");
                }
                w.line(R"(    while (pos_ < source_.size() && source_[pos_] != '\n') ++pos_;)");
                w.line("    continue; }");
                break;
            }
        }
        // Block comment: pattern inner starts with /* or \/\* (escaped-slash + escaped-star).
        // Phase A.4: when DOC_BLOCK is defined, restrict skip to NOT eat
        //   - `/** ... */` outer doc (2 stars, body, terminator) — but `/**/`
        //     (empty) and `/***+` (3+ stars) still skip as ordinary comments
        //   - `/*! ... */` inner doc
        bool has_doc_block = false;
        for (const auto& t : g_.tokens) {
            if (t.kind == int32_t(ast::TOKEN_REGEX) &&
                (t.name == "DOC_BLOCK" || t.name == "DOC_BLOCK_INNER")) {
                has_doc_block = true; break;
            }
        }
        for (const auto& t : g_.tokens) {
            if (t.kind != skip_code) continue;
            auto inner = regex_inner(t.pattern);
            if (inner.starts_with("/*") || inner.starts_with("\\/\\*")) {
                // Nested `/* outer /* inner */ ... */` (B-lx-06): keep a
                // depth counter; close only on the matching `*/`. Track
                // newlines so line_ stays correct across multi-line
                // comments (the prior matcher silently lost them).
                if (has_doc_block) {
                    // Skip `/*` block comment EXCEPT:
                    //   - `/**` followed by non-`*` non-`/` (outer block doc)
                    //   - `/*!` (inner block doc)
                    // `/**/`, `/***+`, and `/*` + ordinary body still skip.
                    w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '*' &&");
                    w.line("    !(pos_+2 < source_.size() && source_[pos_+2] == '*' &&");
                    w.line("      !(pos_+3 < source_.size() &&");
                    w.line("        (source_[pos_+3] == '/' || source_[pos_+3] == '*'))) &&");
                    w.line("    !(pos_+2 < source_.size() && source_[pos_+2] == '!')) {");
                } else {
                    w.line("if (c == '/' && pos_+1 < source_.size() && source_[pos_+1] == '*') {");
                }
                w.line("    pos_ += 2;");
                w.line("    int depth = 1;");
                w.line("    while (depth > 0 && pos_+1 < source_.size()) {");
                w.line("        if (source_[pos_] == '/' && source_[pos_+1] == '*') { ++depth; pos_ += 2; }");
                w.line("        else if (source_[pos_] == '*' && source_[pos_+1] == '/') { --depth; pos_ += 2; }");
                w.line(R"(        else { if (source_[pos_] == '\n') ++line_; ++pos_; })");
                w.line("    }");
                w.line("    continue; }");
                break;
            }
        }
        w.line("break;");
        w.dedent();
        w.line("}");
        w.line("if (pos_ >= source_.size()) return {TK::Eof, {}, line_};");
        w.line("size_t   start      = pos_;");
        w.line("uint32_t start_line_ = line_;");
        w.line("char     c           = source_[pos_];");
        w.line("(void)c;");
        w.line();

        // DOC_LINE outer doc-comment — must run BEFORE the keyword/punctuation
        // literal block so the matcher sees `///foo` before the bare `/`
        // SLASH literal claims the leading slash.
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX) || t.name != "DOC_LINE") continue;
            w.line("// DOC_LINE = /\\/\\/\\/[^\\n]*/ (outer doc-comment, runs before SLASH literal)");
            w.line("if (c == '/' && pos_+2 < source_.size() &&");
            w.line("    source_[pos_+1] == '/' && source_[pos_+2] == '/' &&");
            w.line("    !(pos_+3 < source_.size() && source_[pos_+3] == '/')) {");
            w.indent();
            w.line("pos_ += 3;");
            w.line("while (pos_ < source_.size() && source_[pos_] != '\\n') ++pos_;");
            w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
            w.dedent();
            w.line("}");
            w.line();
            break;
        }
        // DOC_INNER inner doc-comment (`//!`) — same placement constraint.
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX) || t.name != "DOC_INNER") continue;
            w.line("// DOC_INNER = /\\/\\/![^\\n]*/ (inner doc-comment, runs before SLASH literal)");
            w.line("if (c == '/' && pos_+2 < source_.size() &&");
            w.line("    source_[pos_+1] == '/' && source_[pos_+2] == '!') {");
            w.indent();
            w.line("pos_ += 3;");
            w.line("while (pos_ < source_.size() && source_[pos_] != '\\n') ++pos_;");
            w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
            w.dedent();
            w.line("}");
            w.line();
            break;
        }
        // DOC_BLOCK outer block doc-comment (`/** ... */`) — must run before
        // the SLASH literal AND the block-comment skip. Skip rule above
        // already passed through any `/**` not followed by `/` or `*`.
        // Body supports nested `/* */` via depth counter.
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX) || t.name != "DOC_BLOCK") continue;
            w.line("// DOC_BLOCK = `/** ... */` (outer block doc-comment)");
            w.line("if (c == '/' && pos_+3 < source_.size() &&");
            w.line("    source_[pos_+1] == '*' && source_[pos_+2] == '*' &&");
            w.line("    source_[pos_+3] != '/' && source_[pos_+3] != '*') {");
            w.indent();
            w.line("pos_ += 3;  // past `/**`");
            w.line("int depth = 1;");
            w.line("while (depth > 0 && pos_+1 < source_.size()) {");
            w.line("    if (source_[pos_] == '/' && source_[pos_+1] == '*') { ++depth; pos_ += 2; }");
            w.line("    else if (source_[pos_] == '*' && source_[pos_+1] == '/') { --depth; pos_ += 2; }");
            w.line("    else { if (source_[pos_] == '\\n') ++line_; ++pos_; }");
            w.line("}");
            w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
            w.dedent();
            w.line("}");
            w.line();
            break;
        }
        // DOC_BLOCK_INNER inner block doc-comment (`/*! ... */`).
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX) || t.name != "DOC_BLOCK_INNER") continue;
            w.line("// DOC_BLOCK_INNER = `/*! ... */` (inner block doc-comment)");
            w.line("if (c == '/' && pos_+2 < source_.size() &&");
            w.line("    source_[pos_+1] == '*' && source_[pos_+2] == '!') {");
            w.indent();
            w.line("pos_ += 3;  // past `/*!`");
            w.line("int depth = 1;");
            w.line("while (depth > 0 && pos_+1 < source_.size()) {");
            w.line("    if (source_[pos_] == '/' && source_[pos_+1] == '*') { ++depth; pos_ += 2; }");
            w.line("    else if (source_[pos_] == '*' && source_[pos_+1] == '/') { --depth; pos_ += 2; }");
            w.line("    else { if (source_[pos_] == '\\n') ++line_; ++pos_; }");
            w.line("}");
            w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
            w.dedent();
            w.line("}");
            w.line();
            break;
        }

        // Keyword literals — longest match first, sorted by length desc.
        std::vector<const TokenDecl*> literals;
        for (const auto& t : g_.tokens)
            if (t.kind == lit_code) literals.push_back(&t);
        std::sort(literals.begin(), literals.end(), [](auto* a, auto* b) {
            return unquote(a->pattern).size() > unquote(b->pattern).size();
        });

        if (!literals.empty()) {
            w.line("// Keyword / punctuation literals.");
            for (const auto* t : literals) {
                std::string pat = std::string(unquote(t->pattern));
                if (pat.size() == 1) {
                    w.fmt("if (c == '{}') {{ ++pos_; return {{TK::{}, source_.substr(start, 1), start_line_}}; }}",
                          escape_char(pat[0]), safe_tok_name(t->name));
                } else {
                    // Word-like keywords (all alnum/underscore) need a boundary check:
                    // the character after the match must not be alnum/underscore.
                    bool is_word = std::all_of(pat.begin(), pat.end(),
                        [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; });
                    // Bounds-guard + direct fixed-length memcmp: char_traits
                    // compare(p, "pat", N) lowers to a single memcmp of the
                    // COMPILE-TIME length N — no runtime strlen, no substr/
                    // string_view temporaries. (string_view::compare(pos,N,s)
                    // calls traits::length(s) per keyword per token; the old
                    // substr-twice idiom built two views.) The `pos_+N <= size`
                    // guard must precede the memcmp so it never reads past EOF.
                    if (is_word) {
                        w.fmt("if (pos_ + {0} <= source_.size() &&", pat.size());
                        w.fmt("    std::char_traits<char>::compare(source_.data() + pos_, \"{1}\", {0}) == 0 &&",
                              pat.size(), pat);
                        w.fmt("    (pos_ + {0} >= source_.size() || (!std::isalnum(source_[pos_ + {0}]) && source_[pos_ + {0}] != '_' && source_[pos_ + {0}] != '$'))) {{",
                              pat.size());
                    } else {
                        w.fmt("if (pos_ + {0} <= source_.size() &&", pat.size());
                        w.fmt("    std::char_traits<char>::compare(source_.data() + pos_, \"{1}\", {0}) == 0) {{",
                              pat.size(), pat);
                    }
                    w.fmt("    pos_ += {}; return {{TK::{}, source_.substr(start, {}), start_line_}}; }}",
                          pat.size(), safe_tok_name(t->name), pat.size());
                }
            }
            w.line();
        }

        // Well-known regex patterns — generated as hand-coded matchers.
        emit_regex_tokens(w);

        w.line("++pos_; // unknown character — surface it to the parser");
        w.line("return {TK::Invalid, source_.substr(start, 1), start_line_};");
        w.dedent();
        w.line("}");
        w.line();
    }

    // Return name of the first FLOAT-like regex token (pattern has '.' and '[0-9]'), or "".
    std::string find_float_token_name() const {
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX)) continue;
            std::string_view pat = std::string_view(t.pattern);
            if (pat.size() >= 2 && pat.front() == '/' && pat.back() == '/')
                pat = pat.substr(1, pat.size() - 2);
            if (pat.find('.') != std::string::npos && pat.find("[0-9]") != std::string::npos)
                return safe_tok_name(t.name);
        }
        return {};
    }

    // Detect regex features for enhanced number lexing.
    static bool pat_has_hex(std::string_view pat)  { return pat.find("a-f") != std::string::npos || pat.find("a-F") != std::string::npos; }
    static bool pat_has_bin(std::string_view pat)  { return pat.find("0[bB]") != std::string::npos || pat.find("0b") != std::string::npos; }
    static bool pat_has_oct(std::string_view pat)  { return pat.find("0[oO]") != std::string::npos || pat.find("0o") != std::string::npos; }
    static bool pat_is_integer_regex(std::string_view pat) {
        const bool has_digits = pat.find("[0-9]+") != std::string::npos ||
                                pat.find("[0-9][0-9_]*") != std::string::npos;
        const bool has_decimal_point = pat.find("\\.") != std::string::npos;
        return has_digits && !has_decimal_point;
    }
    static bool pat_has_int_suffix(std::string_view pat) {
        return pat.find("i8") != std::string::npos    || pat.find("i16") != std::string::npos ||
               pat.find("i32") != std::string::npos   || pat.find("i64") != std::string::npos ||
               pat.find("u8") != std::string::npos    || pat.find("u16") != std::string::npos ||
               pat.find("u32") != std::string::npos   || pat.find("u64") != std::string::npos ||
               pat.find("usize") != std::string::npos || pat.find("isize") != std::string::npos ||
               pat.find("ull") != std::string::npos   || pat.find("_u") != std::string::npos;
    }
    static bool pat_has_float_suffix(std::string_view pat) {
        return pat.find("f32") != std::string::npos || pat.find("f64") != std::string::npos ||
               pat.find("[fd]") != std::string::npos;
    }

    // Emit suffix matching for integer tokens.
    // Supports Rust-style suffixes (i8/i16/i32/i64/u8/u16/u32/u64/usize/isize)
    // plus older C-style suffixes kept for backwards compatibility.
    static void emit_int_suffix_matching(CodeWriter& w) {
        // Helper lambda in generated code: try to match a suffix string.
        w.line("// Integer type suffix (longest match).");
        w.line("// A digit-run-trailing '_' followed by s/u starts a _sNN/_uNN suffix —");
        w.line("// give it back to the suffix matcher (writ-style _s64; harmless otherwise).");
        w.line("if (pos_ > start && source_[pos_-1] == '_' && pos_ < source_.size() &&");
        w.line("    (source_[pos_] == 's' || source_[pos_] == 'u')) --pos_;");
        w.line("auto try_suffix = [&](std::string_view sfx) -> bool {");
        w.line("    if (pos_ + sfx.size() <= source_.size() &&");
        w.line("        source_.substr(pos_, sfx.size()) == sfx) {");
        w.line("        pos_ += sfx.size(); return true;");
        w.line("    }");
        w.line("    return false;");
        w.line("};");
        // Ordered longest-first. The C-style 'u' suffix must be checked last
        // and must NOT be followed by an alnum/underscore (to avoid eating IDENT).
        w.line("if (!try_suffix(\"usize\") && !try_suffix(\"isize\") &&");
        w.line("    !try_suffix(\"_u64\") && !try_suffix(\"_u32\") && !try_suffix(\"_u16\") && !try_suffix(\"_u8\") &&");
        w.line("    !try_suffix(\"_s64\") && !try_suffix(\"_s32\") && !try_suffix(\"_s16\") && !try_suffix(\"_s8\") &&");
        w.line("    !try_suffix(\"i128\") && !try_suffix(\"i64\") && !try_suffix(\"i56\") && !try_suffix(\"i32\") && !try_suffix(\"i24\") && !try_suffix(\"i16\") && !try_suffix(\"i8\") &&");
        w.line("    !try_suffix(\"u128\") && !try_suffix(\"u64\") && !try_suffix(\"u56\") && !try_suffix(\"u32\") && !try_suffix(\"u24\") && !try_suffix(\"u16\") && !try_suffix(\"u8\") &&");
        w.line("    !try_suffix(\"ull\") && !try_suffix(\"ul\") && !try_suffix(\"ll\")) {");
        w.line("    // Single-char 'u' — only if not followed by alnum/underscore.");
        w.line("    if (pos_ < source_.size() && source_[pos_] == 'u' &&");
        w.line("        (pos_+1 >= source_.size() || (!std::isalnum(source_[pos_+1]) && source_[pos_+1] != '_' && source_[pos_+1] != '$')))");
        w.line("        ++pos_;");
        w.line("}");
    }

    void emit_regex_tokens(CodeWriter& w) {
        const std::string float_tok = find_float_token_name();

        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_REGEX)) continue;
            std::string_view pat = std::string_view(t.pattern);
            // Strip /.../ delimiters.
            if (pat.size() >= 2 && pat.front() == '/' && pat.back() == '/')
                pat = pat.substr(1, pat.size() - 2);

            // DOC_LINE / DOC_INNER / DOC_BLOCK / DOC_BLOCK_INNER are emitted
            // earlier in lex_one (before keyword literals) so the bare `/`
            // SLASH literal doesn't claim the leading slash.
            if (t.name == "DOC_LINE" || t.name == "DOC_INNER" ||
                t.name == "DOC_BLOCK" || t.name == "DOC_BLOCK_INNER") continue;

            // IDENT-like: [a-zA-Z_][a-zA-Z0-9_]*
            if (pat == "[a-zA-Z_][a-zA-Z0-9_]*" || pat == "[a-zA-Z_]\\w*") {
                w.fmt("// {} = /{}/", t.name, pat);
                w.fmt("if (std::isalpha(c) || c == '_') {{");
                w.indent();
                w.line("while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_' || source_[pos_] == '$'))");
                w.line("    ++pos_;");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // INTEGER: patterns containing [0-9]+ without '.' are integer tokens.
            // Supports optional base prefixes (0x, 0b, 0o) and type suffixes
            // (_u8..._u64, _s8..._s64, ull, ul, ll, u) when detected in the regex.
            // If a FLOAT token also exists, performs longest-match: after decimal digits,
            // if '.' followed by a digit is found, consumes fractional part and returns FLOAT.
            else if (pat_is_integer_regex(pat)) {
                const bool hex = pat_has_hex(pat);
                const bool bin = pat_has_bin(pat);
                const bool oct = pat_has_oct(pat);
                const bool int_sfx = pat_has_int_suffix(pat);
                // Float suffix is detected from the FLOAT token pattern.
                std::string_view float_pat;
                for (const auto& ft : g_.tokens) {
                    if (ft.kind != int32_t(ast::TOKEN_REGEX)) continue;
                    float_pat = ft.pattern;
                    if (float_pat.size() >= 2 && float_pat.front() == '/' && float_pat.back() == '/')
                        float_pat = float_pat.substr(1, float_pat.size() - 2);
                    if (float_pat.find('.') != std::string::npos && float_pat.find("[0-9]") != std::string::npos)
                        break;
                    float_pat = {};
                }
                const bool flt_sfx = pat_has_float_suffix(float_pat);

                w.fmt("// {} = /{}/", t.name, pat);
                // Leading-dot floats (.5): emitted ONLY when the grammar has no "."
                // literal token (e.g. the writ SDN grammar) AND the FLOAT pattern
                // allows an empty integer part ([0-9]*\.). With a DOT token (logos),
                // t.1.0 must stay DOT + INTEGER + DOT + INTEGER.
                bool grammar_has_dot_literal = false;
                for (const auto& lt : g_.tokens) {
                    if (lt.kind == int32_t(ast::TOKEN_LITERAL) &&
                        (lt.pattern == "\".\"" || lt.pattern == ".")) {
                        grammar_has_dot_literal = true;
                        break;
                    }
                }
                const bool leading_dot_float =
                    !float_pat.empty() && !grammar_has_dot_literal &&
                    float_pat.find("[0-9]*\\.") != std::string_view::npos;
                if (leading_dot_float) {
                    w.line("if (c == '.' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1])) {");
                    w.indent();
                    w.line("++pos_; // consume '.'");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {");
                    w.line("    ++pos_;");
                    w.line("    if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;");
                    w.line("    while (pos_ < source_.size() && std::isdigit(source_[pos_])) ++pos_;");
                    w.line("}");
                    if (flt_sfx)
                        w.line("if (pos_ < source_.size() && (source_[pos_] == 'f' || source_[pos_] == 'd')) ++pos_;");
                    w.line("return {TK::FLOAT, source_.substr(start, pos_ - start), start_line_};");
                    w.dedent();
                    w.line("}");
                }
                // Entry condition: digit or negative-digit.
                if (!float_tok.empty()) {
                    w.fmt("if (std::isdigit(c) || (c == '-' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))) {{");
                } else {
                    w.fmt("if (std::isdigit(c) || (c == '-' && pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))) {{");
                }
                w.indent();
                w.line("if (c == '-') ++pos_;");

                if (hex || bin || oct) {
                    // Base prefix detection.
                    w.line("int base = 10;");
                    w.line("if (pos_ < source_.size() && source_[pos_] == '0' && pos_+1 < source_.size()) {");
                    w.indent();
                    w.line("char nx = source_[pos_+1];");
                    if (hex) w.line("if (nx == 'x' || nx == 'X') { base = 16; pos_ += 2; }");
                    if (bin) w.fmt("{}if (nx == 'b' || nx == 'B') {{ base = 2; pos_ += 2; }}", hex ? "else " : "");
                    if (oct) w.fmt("{}if (nx == 'o' || nx == 'O') {{ base = 8; pos_ += 2; }}", (hex || bin) ? "else " : "");
                    w.dedent();
                    w.line("}");
                    // Consume digits per base.
                    w.line("if (base == 16) {");
                    w.line("    while (pos_ < source_.size() && (std::isxdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("} else if (base == 2) {");
                    w.line("    while (pos_ < source_.size() && (source_[pos_] == '0' || source_[pos_] == '1' || source_[pos_] == '_')) ++pos_;");
                    w.line("} else if (base == 8) {");
                    w.line("    while (pos_ < source_.size() && ((source_[pos_] >= '0' && source_[pos_] <= '7') || source_[pos_] == '_')) ++pos_;");
                    w.line("} else {");
                    w.line("    while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("}");
                } else {
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                }

                if (!float_tok.empty()) {
                    // Longest-match: decimal-only → if next is '.' followed by digit, it's a float.
                    // But NOT if the token started right after a '.': that's a tuple index (t.1.0).
                    if (hex || bin || oct) {
                        w.line("if (base == 10 && pos_ < source_.size() && source_[pos_] == '.'");
                    } else {
                        w.line("if (pos_ < source_.size() && source_[pos_] == '.'");
                    }
                    w.line("    && pos_+1 < source_.size() && std::isdigit(source_[pos_+1])");
                    w.line("    && !(start > 0 && source_[start - 1] == '.')) {");
                    w.indent();
                    w.line("++pos_; // consume '.'");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {");
                    w.indent();
                    w.line("++pos_;");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    w.dedent();
                    w.line("}");
                    if (flt_sfx) {
                        w.line("if (pos_ + 3 <= source_.size() &&");
                        w.line("    (source_.substr(pos_, 3) == \"f32\" || source_.substr(pos_, 3) == \"f64\")) {");
                        w.line("    pos_ += 3;");
                        w.line("} else if (pos_ < source_.size() && (source_[pos_] == 'f' || source_[pos_] == 'd')) {");
                        w.line("    ++pos_;");
                        w.line("}");
                    }
                    w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", float_tok);
                    w.dedent();
                    w.line("}");
                    // Bare-mantissa exponent FLOAT (no decimal point): `5e9`,
                    // `5e-11`, `2E+3` → FLOAT. Only base-10 decimal integers
                    // upgrade; requires `e`/`E` then (optional sign and) a digit.
                    if (hex || bin || oct)
                        w.line("if (base == 10 && pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')");
                    else
                        w.line("if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')");
                    w.line("    && ((pos_+1 < source_.size() && std::isdigit(source_[pos_+1]))");
                    w.line("        || (pos_+2 < source_.size() && (source_[pos_+1] == '+' || source_[pos_+1] == '-') && std::isdigit(source_[pos_+2])))) {");
                    w.indent();
                    w.line("++pos_; // consume 'e'/'E'");
                    w.line("if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;");
                    w.line("while (pos_ < source_.size() && (std::isdigit(source_[pos_]) || source_[pos_] == '_')) ++pos_;");
                    if (flt_sfx) {
                        w.line("if (pos_ + 3 <= source_.size() &&");
                        w.line("    (source_.substr(pos_, 3) == \"f32\" || source_.substr(pos_, 3) == \"f64\")) {");
                        w.line("    pos_ += 3;");
                        w.line("} else if (pos_ < source_.size() && (source_[pos_] == 'f' || source_[pos_] == 'd')) {");
                        w.line("    ++pos_;");
                        w.line("}");
                    }
                    w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", float_tok);
                    w.dedent();
                    w.line("}");
                }

                if (int_sfx) {
                    // Integer type suffixes — try longest match.
                    emit_int_suffix_matching(w);
                }

                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // FLOAT: handled by the INTEGER longest-match handler above — skip.
            else if (pat.find('.') != std::string::npos && pat.find("[0-9]") != std::string::npos) {
                w.fmt("// {} = /{}/ — handled by INTEGER longest-match above", t.name, pat);
            }
            // RAW_STRING: r"...", r#"..."#, r##"..."##, etc.
            // Count '#' after 'r' to determine delimiter depth.
            else if (pat.size() >= 4 && pat[0] == 'r' && pat[1] == '"') {
                w.fmt("// {} = /{}/ (also r#\"...\"#, r##\"...\"##, ...)", t.name, pat);
                w.line(R"(if (c == 'r' && pos_+1 < source_.size() && (source_[pos_+1] == '"' || source_[pos_+1] == '#')) {)");
                w.indent();
                w.line("size_t hashes = 0;");
                w.line("size_t p = pos_ + 1;");
                w.line("while (p < source_.size() && source_[p] == '#') { ++hashes; ++p; }");
                w.line(R"(if (p < source_.size() && source_[p] == '"') {)");
                w.indent();
                w.line("pos_ = p + 1; // skip r###...\"");
                w.line("bool found = false;");
                w.line("while (!found && pos_ < source_.size()) {");
                w.indent();
                w.line(R"(if (source_[pos_] == '"') {)");
                w.line("    size_t h = 0;");
                w.line("    while (h < hashes && pos_+1+h < source_.size() && source_[pos_+1+h] == '#') ++h;");
                w.line("    if (h == hashes) { pos_ += 1 + hashes; found = true; }");
                w.line("    else ++pos_;");
                // Count newlines inside the raw-string body, or every line after a
                // multi-line r#"..."# drifts (the body scan moves pos_ past '\n').
                w.line("} else { if (source_[pos_] == '\\n') ++line_; ++pos_; }");
                w.dedent();
                w.line("}");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
            }
            // PREFIXED STRING (e.g. BYTE_STRING `b"..."`): single-letter
            // prefix followed by `"..."` with the same escape rules as
            // STRING. Detect by pattern shape `<letter>"...".
            else if (pat.size() >= 4 && std::isalpha(static_cast<unsigned char>(pat[0])) &&
                     pat[1] == '"' && pat[0] != 'r') {
                char prefix = pat[0];
                w.fmt("// {} = /{}/", t.name, pat);
                w.fmt("if (c == '{}' && pos_+1 < source_.size() && source_[pos_+1] == '\"') {{", prefix);
                w.indent();
                w.line("pos_ += 2;");
                // B-lx-05: bound the unterminated-string scan to a single
                // line so the diagnostic doesn't span the rest of the file.
                w.line(R"(while (pos_ < source_.size() && source_[pos_] != '"' && source_[pos_] != '\n') {)");
                w.line(R"(    if (source_[pos_] == '\\' && pos_+1 < source_.size() && source_[pos_+1] != '\n') ++pos_;)");
                w.line("    ++pos_;");
                w.line("}");
                w.line(R"(if (pos_ < source_.size() && source_[pos_] == '"') ++pos_;)");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // STRING: \"([^\"\\\\]|\\\\.)*\"
            else if (pat.find('"') != std::string::npos) {
                w.fmt("// {} = /{}/", t.name, pat);
                w.line(R"(if (c == '"') {)");
                w.indent();
                w.line("++pos_;");
                // B-lx-05: bound the unterminated-string scan to a single
                // line so the diagnostic doesn't span the rest of the file.
                w.line(R"(while (pos_ < source_.size() && source_[pos_] != '"' && source_[pos_] != '\n') {)");
                w.line(R"(    if (source_[pos_] == '\\' && pos_+1 < source_.size() && source_[pos_+1] != '\n') ++pos_;)");
                w.line("    ++pos_;");
                w.line("}");
                w.line(R"(if (pos_ < source_.size() && source_[pos_] == '"') ++pos_;)");
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            // CHAR_LIT-like: starts with apostrophe, then `(...)` regex group
            // (e.g. `/'(\\.|[^'\\])'/`). Matched BEFORE LIFETIME so `'A'` is
            // a char-lit and `'a` is a lifetime. The matcher is non-consuming
            // when no closing apostrophe is found — we leave pos_ untouched
            // and fall through to LIFETIME.
            else if (pat.size() >= 2 && pat.front() == '\'' && pat[1] == '(') {
                w.fmt("// {} = /{}/  (UTF-8 codepoint allowed in plain-char alt)", t.name, pat);
                w.line("if (c == '\\'' && pos_ + 1 < source_.size()) {");
                w.indent();
                w.line("size_t save = pos_;");
                w.line("size_t p = pos_ + 1;");
                w.line("bool ok = false;");
                // Escape: `\<any>` is one char by default; `\xNN` consumes the
                // two hex digits; `\u{HHH...}` consumes up to the closing `}`.
                // The wider escape forms mirror Rust's char-literal syntax —
                // needed for porting coretests that spell out non-printable
                // codepoints directly (e.g. `'\u{2007}'` FIGURE SPACE).
                w.line("if (p + 2 < source_.size() && source_[p] == '\\\\') {");
                w.line("    char esc = source_[p + 1];");
                w.line("    if (esc == 'x') {");
                w.line("        // `\\xNN` — 2 hex digits.");
                w.line("        size_t hx = p + 2;");
                w.line("        if (hx + 2 < source_.size() && std::isxdigit((unsigned char)source_[hx]) && std::isxdigit((unsigned char)source_[hx + 1]) && source_[hx + 2] == '\\'') {");
                w.line("            pos_ = hx + 3; ok = true;");
                w.line("        }");
                w.line("    } else if (esc == 'u' && p + 2 < source_.size() && source_[p + 2] == '{') {");
                w.line("        // `\\u{HHH...}` — 1..6 hex digits in braces.");
                w.line("        size_t hx = p + 3;");
                w.line("        size_t hx_end = hx;");
                w.line("        while (hx_end < source_.size() && std::isxdigit((unsigned char)source_[hx_end]) && hx_end - hx < 6) ++hx_end;");
                w.line("        if (hx_end > hx && hx_end + 1 < source_.size() && source_[hx_end] == '}' && source_[hx_end + 1] == '\\'') {");
                w.line("            pos_ = hx_end + 2; ok = true;");
                w.line("        }");
                w.line("    } else {");
                w.line("        p += 2;");
                w.line("        if (source_[p] == '\\'') { pos_ = p + 1; ok = true; }");
                w.line("    }");
                // Plain char: a single Unicode codepoint that isn't `'` or `\`.
                // Lead byte determines UTF-8 length (1..4 bytes).
                w.line("} else if (p < source_.size() && source_[p] != '\\'' && source_[p] != '\\\\') {");
                w.line("    unsigned char lead = static_cast<unsigned char>(source_[p]);");
                w.line("    size_t cp_len = 0;");
                w.line("    if      ((lead & 0x80) == 0x00) cp_len = 1;");
                w.line("    else if ((lead & 0xE0) == 0xC0) cp_len = 2;");
                w.line("    else if ((lead & 0xF0) == 0xE0) cp_len = 3;");
                w.line("    else if ((lead & 0xF8) == 0xF0) cp_len = 4;");
                w.line("    if (cp_len > 0 && p + cp_len < source_.size()) {");
                w.line("        bool valid = true;");
                w.line("        for (size_t i = 1; i < cp_len; ++i) {");
                w.line("            if ((static_cast<unsigned char>(source_[p + i]) & 0xC0) != 0x80) {");
                w.line("                valid = false; break;");
                w.line("            }");
                w.line("        }");
                w.line("        if (valid && source_[p + cp_len] == '\\'') {");
                w.line("            pos_ = p + cp_len + 1; ok = true;");
                w.line("        }");
                w.line("    }");
                w.line("}");
                w.fmt("if (ok) return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};",
                      safe_tok_name(t.name));
                w.line("pos_ = save;");
                w.dedent();
                w.line("}");
            }
            // LIFETIME-like: starts with apostrophe, then [a-z][a-z0-9_]*
            // Matches Rust-style lifetime annotations: 'a, 'static, 'lifetime_name
            // NOTE: c = source_[pos_] is read WITHOUT incrementing pos_, so pos_
            // still points at the apostrophe.  We must check pos_+1 for the letter
            // after it, then advance pos_ past the apostrophe before the while loop.
            else if (pat.size() >= 2 && pat.front() == '\'' && pat[1] == '[') {
                bool lower_start = pat.find("[a-z") != std::string::npos;
                // `'_` underscore-lifetime support: any "[a-z_…" char class
                // in the start position admits underscore as a valid first
                // character (L3, P4-pm-X follow-up).
                bool underscore_start = pat.find("[a-z_") != std::string::npos
                                     || pat.find("[_a-z") != std::string::npos;
                bool alnum_rest  = pat.find("[a-z0-9_]") != std::string::npos
                                || pat.find("[a-zA-Z0-9_]") != std::string::npos;
                w.fmt("// {} = /{}/", t.name, pat);
                if (underscore_start) {
                    w.line("if (c == '\\'' && pos_ + 1 < source_.size() && (std::islower(source_[pos_ + 1]) || source_[pos_ + 1] == '_')) {");
                } else if (lower_start) {
                    w.line("if (c == '\\'' && pos_ + 1 < source_.size() && std::islower(source_[pos_ + 1])) {");
                } else {
                    w.line("if (c == '\\'' && pos_ + 1 < source_.size() && std::isalpha(source_[pos_ + 1])) {");
                }
                w.indent();
                w.line("++pos_;  // advance past apostrophe");
                if (alnum_rest) {
                    w.line("while (pos_ < source_.size() && (std::isalnum(source_[pos_]) || source_[pos_] == '_' || source_[pos_] == '$'))");
                    w.line("    ++pos_;");
                } else {
                    w.line("if (pos_ < source_.size() && std::isalpha(source_[pos_])) ++pos_;");
                }
                w.fmt("return {{TK::{}, source_.substr(start, pos_ - start), start_line_}};", safe_tok_name(t.name));
                w.dedent();
                w.line("}");
            }
            else {
                w.fmt("// TODO: {} = /{}/  — add hand-coded matcher", t.name, pat);
            }
            w.line();
        }
    }

    // ── Public entry points ───────────────────────────────────────────────────

    void emit_public_entries(CodeWriter& w) {
        if (g_.exports.empty()) return;
        w.line("// ── Public entry points ───────────────────────────────────────────────────");
        w.line();
        for (const auto& e : g_.exports) {
            // `arena: external`: the doc is the caller's, so the entry hands back
            // the root edge and never touches set_root — the caller wires it in
            // (or, for an embedded sub-parse, splices it into the outer node).
            if (g_.arena_ext) {
                w.fmt("logos::writ::AnyVal {}::parse_{}() {{", parser_class_, e);
                w.indent();
                w.fmt("AnyVal root = rule_{}();", e);
                w.line("if (root.is_null() || !at_eof()) return AnyVal{};");
                w.line("return root;");
                w.dedent();
                w.line("}");
                w.line();
                continue;
            }
            w.fmt("logos::writ::Writ {}::parse_{}() {{", parser_class_, e);
            w.indent();
            // MultiChunk (never-move: the parser holds node ptrs/views across
            // allocations + ObjectArray/Map::grow assume the header never moves; a
            // GrowableSingleChunk realloc would dangle them — MultiChunk APPENDS chunks
            // so existing objects never move) WITH A LARGE FIRST CHUNK. The metaprog
            // dispatcher AND the metacall splice address AST nodes by FLAT
            // head_base + offset (node.offset()/item_offset), which is only valid while
            // the whole AST lives in chunk[0]; a node spilled into chunk 2+ yields a
            // negative/garbage flat offset → wild ptr. So size chunk[0] large enough
            // that realistic ASTs never spill. Lazy-zero (arena.cpp) keeps it cheap
            // (only touched pages commit). A genuinely larger AST still works for
            // non-metaprog code; only the flat-offset metaprog paths need single-chunk.
            w.line("doc_ = logos::writ::make_doc(64ull * 1024 * 1024).get();");
            w.fmt("AnyVal root = rule_{}();", e);
            // Recovery instead of assertion (Meta-Sprint M0.2): parse failure
            // returns an empty Writ doc; the caller's ast.is_null() check
            // takes the error path. Closes B-mv-05/06/07/08 and B-lx-01/02.
            w.line("if (root.is_null()) {");
            w.indent();
            w.fmt("std::fprintf(stderr, \"parse error in {}: expected {} (near line %u)\\n\",",
                  e, e);
            w.line("             next_line());");
            w.line("return logos::writ::Writ{};  // null doc; caller handles error");
            w.dedent();
            w.line("}");
            w.line("doc_.set_root(root);");
            w.line("return std::move(doc_);");
            w.dedent();
            w.line("}");
            w.line();
        }
    }

    // ── Rule implementations ──────────────────────────────────────────────────

    void emit_rules(CodeWriter& w) {
        w.line("// ── Grammar rules ─────────────────────────────────────────────────────────");
        w.line();
        for (const auto& rule : g_.rules) emit_rule(w, rule);
    }

    void emit_rule(CodeWriter& w, const Rule& rule) {
        lc_ = 0;  // reset label counter for this rule
        cur_rule_group_ = rule.group;

        // Packrat wrapper.  Key is the logical position of the next
        // token-to-lex — NOT `pos_` directly, because `pos_` points past
        // any prefetched lookahead (`have_la_ == true`), which can alias
        // with a genuine "no lookahead at same pos_" entry from a
        // different logical context.  We normalise by backing pos_ to
        // the start of `la_`'s token when a lookahead is live.
        //
        // On cache hit we clear have_la_ so the next peek re-lexes at
        // the stored end position; that also means end position is
        // always "before any prefetch", matching the key convention.
        const bool memo = is_memoized(rule.name);
        if (memo) {
            w.fmt("AnyVal {}::rule_{}() {{", parser_class_, rule.name);
            w.indent();
            // Un-prefetch: back pos_ to the lookahead token's start. line_ must
            // follow (= la_.line, the token's start line) or a MULTI-LINE
            // lookahead (raw string) leaves line_ at its end while pos_ is at its
            // start, so re-lexing re-counts its newlines → drift after the token.
            w.line("if (have_la_) { pos_ = static_cast<size_t>(la_.text.data() - source_.data()); line_ = la_.line; have_la_ = false; }");
            w.line("size_t start = pos_;");
            w.fmt("auto& slot = memo_{}_[start];", rule.name);
            w.line("if (slot.end != kMemoEmpty) {");
            w.indent();
            w.line("pos_ = slot.end;");
            w.line("line_ = slot.line;   // restore line at end (else newlines spanned by the cached parse go uncounted)");
            w.line("have_la_ = false;");
            w.line("return slot.first;");
            w.dedent();
            w.line("}");
            w.fmt("AnyVal result = rule_{}_impl();", rule.name);
            w.line("if (have_la_) { pos_ = static_cast<size_t>(la_.text.data() - source_.data()); line_ = la_.line; have_la_ = false; }");
            w.fmt("memo_{}_[start] = MemoCell{{result, pos_, line_}};", rule.name);
            w.line("return result;");
            w.dedent();
            w.line("}");
            w.line();
        }

        const std::string fn = memo ? rule.name + "_impl" : rule.name;
        w.fmt("AnyVal {}::rule_{}() {{", parser_class_, fn);
        w.indent();
        w.line("[[maybe_unused]] size_t saved_pos = 0;");
        w.line("[[maybe_unused]] bool   saved_la  = false;");
        w.line("[[maybe_unused]] size_t saved_doc_ = 0;");
        w.line();

        for (size_t i = 0; i < rule.alts.size(); ++i)
            emit_alt(w, rule, rule.alts[i], i);

        w.line("return AnyVal{}; // no alternative matched");
        w.dedent();
        w.line("}");
        w.line();
    }

    void emit_alt(CodeWriter& w, const Rule& rule, const Item::Alt& alt, size_t idx) {
        // Comment showing the PEG source of this alternative.
        w.fmt("// Alternative {}: {}", idx + 1, peg_text(alt));
        // Outer block: holds saved_tok so it's accessible in the restore code below.
        w.line("{");
        w.indent();
        w.line("saved_pos  = pos_;");
        w.line("saved_la   = have_la_;");
        w.line("saved_doc_ = doc_.arena_checkpoint();");
        w.line("[[maybe_unused]] Token    saved_tok_  = la_;");
        w.line("[[maybe_unused]] uint32_t saved_line_ = line_;");
        w.line();
        // Inner block: all captures and node pointers are scoped here.
        // The backtrack label below is OUTSIDE this block so gotos don't cross inits.
        w.line("{");
        w.indent();
        // first_line_: line of the first token of this alt's match (for SRC_LINE in AST nodes).
        w.line("[[maybe_unused]] uint32_t first_line_ = peek_token().line;");

        // Backtrack label for this alternative — used as fail_label for all items.
        std::string alt_fail = std::format("bt_{}_{}", rule.name, idx);

        // If the action uses $..., declare a rule-captures collector array.
        // RULE_REF results anywhere in the sequence push to it; TOKEN_REF results don't.
        rcap_var_.clear();
        if (alt.action && action_has_array_capture(*alt.action)) {
            rcap_var_ = "rcap_" + std::to_string(lc_++);
            w.fmt("auto {} = doc_.make_array(4).get();", rcap_var_);
        }

        // Capture slots (one per item in the sequence).
        // We number them from 1 ($1, $2, ...) to match grammar action syntax.
        // All captures have type AnyVal — tokens are interned as arena strings,
        // rule results are stored as arena object offsets.
        std::vector<std::string> captures(alt.seq.size() + 1); // 1-indexed
        for (size_t i = 0; i < alt.seq.size(); ++i) {
            std::string cap = std::format("cap{}", i + 1);
            captures[i + 1] = cap;
            // For fold-mode REP/OPT: provide the preceding item's cap as the fold initialiser.
            if ((alt.seq[i].kind == int32_t(ast::REP) || alt.seq[i].kind == int32_t(ast::OPT))
                && rep_is_fold(alt.seq[i]) && i > 0)
                fold_init_cap_ = captures[i]; // captures[i] = cap of item i-1 (1-indexed)
            emit_item_match(w, alt.seq[i], cap, alt_fail, i);
            fold_init_cap_.clear();
        }

        // Action: build AST node.  rcap_var_ is still set here for ARRAY_CAPTURE use.
        if (alt.action) {
            emit_action(w, *alt.action, captures, alt.seq);
            rcap_var_.clear();
        } else if (alt.seq.size() == 1) {
            rcap_var_.clear();
            // No action + single item → pass through.
            w.fmt("return {};", captures[1]);
        } else {
            rcap_var_.clear();
            // No action + multiple items → return the last VALUE-carrying capture.
            // Fold chains (`atom <- primary postfix*`) end in the REP; grouping
            // wrappers (`( expr )`) end in a delimiter TOKEN whose capture is a
            // mere interned string. A token-only alt has no value: keep the old
            // behaviour (the last capture) rather than silently returning null.
            size_t lvc = last_value_capture(alt.seq);
            w.fmt("return {};", captures[lvc ? lvc : alt.seq.size()]);
        }

        w.dedent();
        w.line("}"); // end inner block
        // Backtrack label is here, AFTER the inner block — no initialization is crossed.
        w.fmt("[[maybe_unused]] bt_{}_{}:", rule.name, idx);
        w.line("pos_      = saved_pos;");
        w.line("have_la_  = saved_la;");
        w.line("la_       = saved_tok_;");
        w.line("line_     = saved_line_;");
        w.line("// arena_rollback suppressed — AST lives until Writ destruction");
        w.dedent();
        w.line("}");
        w.line();
    }

    // Emit code to match one item; on failure goto fail_label.
    // item_idx is just used for generating unique sub-label names within this item.
    void emit_item_match(CodeWriter& w, const Item& item, const std::string& cap,
                         const std::string& fail_label, size_t item_idx) {
        switch (item.kind) {

        case int32_t(ast::TOKEN_REF): {
            // GT_TYPE is a pseudo-token: matches '>' but also splits '>>' (SHR)
            // into two '>' tokens to allow nested generics like Foo<Bar<T>>.
            if (item.name == "GT_TYPE") {
                w.fmt("if (!try_token_gt()) goto {};", fail_label);
                w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
                break;
            }
            // LT_TYPE: mirror of GT_TYPE for opening brackets.
            if (item.name == "LT_TYPE") {
                w.fmt("if (!try_token_lt()) goto {};", fail_label);
                w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
                break;
            }
            // Raw-group pseudo-tokens: capture the balanced delimiter
            // contents as raw source text. The result `cap` is a
            // string-valued AnyVal (offset to an arena-interned slice
            // of the original source). Each variant differs only in
            // the opening delim it expects.
            if (item.name == "RAW_GROUP_PAREN" ||
                item.name == "RAW_GROUP_BRACKET" ||
                item.name == "RAW_GROUP_BRACE") {
                const char* fn =
                    item.name == "RAW_GROUP_PAREN"   ? "try_raw_group_paren" :
                    item.name == "RAW_GROUP_BRACKET" ? "try_raw_group_bracket"
                                                    : "try_raw_group_brace";
                w.fmt("std::string_view rg_{0}_text;", cap);
                w.fmt("if (!{0}(rg_{1}_text)) goto {2};", fn, cap, fail_label);
                w.fmt("[[maybe_unused]] AnyVal {0} = doc_.make_string(rg_{0}_text).get().to_anyval();", cap);
                break;
            }
            // Match token, intern text as arena string → AnyVal offset.
            w.fmt("if (peek_token().kind != TK::{}) goto {};",
                  safe_tok_name(item.name), fail_label);
            w.fmt("Token tok_{0}_ = next_token();", cap);
            w.fmt("[[maybe_unused]] AnyVal {0} = doc_.make_string(tok_{0}_.text).get().to_anyval();", cap);
            break;
        }

        case int32_t(ast::RULE_REF): {
            // Cross-grammar ref → a keyword-delimited EMBED, not a nested call
            // on the shared token stream. Scan the raw sub-range to the next
            // structural stop, hand that substring to the imported grammar's
            // own parser over OUR document, and resume at the stop.
            if (!item.grammar_alias.empty()) {
                emit_embed_call(w, item, cap, fail_label);
                break;
            }
            std::string call = std::format("rule_{}()", item.name);
            w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, call);
            w.fmt("if ({}.is_null()) goto {};", cap, fail_label);
            // Collect into rule-captures array if $... is used in this alt's
            // action.  Skip for token-alias rules: a `trait_kw <- KW_TRAIT /
            // KW_GENOS` call is treated like a token match by the caller —
            // its result text shouldn't pollute the parent's ITEMS list.
            if (!rcap_var_.empty() && item.grammar_alias.empty()
                && !is_token_alias(item.name))
                w.fmt("{}.push_back({}).get();", rcap_var_, cap);
            break;
        }

        case int32_t(ast::LITERAL): {
            // Match literal string, intern as arena string → AnyVal offset.
            size_t n = item.name.size();
            w.fmt("if (pos_ + {0} > source_.size() || source_.substr(pos_, {0}) != \"{1}\") goto {2};",
                  n, item.name, fail_label);
            w.fmt("[[maybe_unused]] AnyVal {0} = doc_.make_string(source_.substr(pos_, {1})).get().to_anyval();", cap, n);
            w.fmt("pos_ += {};", n);
            break;
        }

        case int32_t(ast::OPT): {
            // Optional: try sub-item, silently ignore failure. cap = null AnyVal if no match.
            std::string id       = fresh();
            std::string done_lbl = "opt_done_" + id;
            std::string fail_lbl = "opt_fail_" + id;

            // Fold-mode OPT: body GROUP references $0 via FOLD_CAPTURE, so the
            // pre-OPT capture must be threaded through.  Matches rep_is_fold's
            // logic; both quantifiers share the same predicate.
            const bool fold_mode = rep_is_fold(item) && !fold_init_cap_.empty();
            if (fold_mode) {
                std::string fold_acc = "opt_acc_" + id;
                w.fmt("AnyVal {} = {};", fold_acc, fold_init_cap_);
                cur_fold_var_ = fold_acc;
                w.line("{");
                w.indent();
                w.line("[[maybe_unused]] size_t opt_pos_ = pos_; [[maybe_unused]] bool opt_la_ = have_la_; [[maybe_unused]] Token opt_tok_ = la_; [[maybe_unused]] uint32_t opt_line_ = line_;");
                w.line("[[maybe_unused]] size_t opt_doc_ = doc_.arena_checkpoint();");
                w.line("{");
                w.indent();
                if (!item.sub_items.empty()) {
                    std::string sub_cap = cap + "_s";
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("{} = {};", fold_acc, sub_cap);
                }
                w.fmt("goto {};", done_lbl);
                w.dedent();
                w.line("}");
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = opt_pos_; have_la_ = opt_la_; la_ = opt_tok_; line_ = opt_line_;");
                w.line("// arena_rollback suppressed — AST lives until Writ destruction");
                w.fmt("{}: ;", done_lbl);
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, fold_acc);
                cur_fold_var_.clear();
                break;
            }

            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("[[maybe_unused]] size_t opt_pos_ = pos_; [[maybe_unused]] bool opt_la_ = have_la_; [[maybe_unused]] Token opt_tok_ = la_; [[maybe_unused]] uint32_t opt_line_ = line_;");
            w.line("[[maybe_unused]] size_t opt_doc_ = doc_.arena_checkpoint();");
            w.line("{"); // inner scope for sub-item
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = cap + "_s";
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                w.fmt("{} = {};", cap, sub_cap);
            }
            w.fmt("goto {};", done_lbl);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = opt_pos_; have_la_ = opt_la_; la_ = opt_tok_; line_ = opt_line_;");
            w.line("// arena_rollback suppressed — AST lives until Writ destruction");
            w.fmt("{}: ;", done_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::REP): {
            std::string id       = fresh();
            std::string fail_lbl = "rep_fail_" + id;

            if (rep_is_fold(item) && !fold_init_cap_.empty()) {
                // ── Fold-mode repetition ─────────────────────────────────────────
                // Accumulator starts from the preceding sequence item.
                // Each iteration: GROUP matches a postfix suffix, builds a new node
                // with RECEIVER=$0 (= fold accumulator), then fold_acc = new node.
                std::string fold_acc     = "fold_acc_" + id;
                std::string matched_var  = "rep_matched_" + id;
                w.fmt("AnyVal {} = {};", fold_acc, fold_init_cap_);
                w.fmt("[[maybe_unused]] bool {} = false;", matched_var);
                // Set cur_fold_var_ so that $0 inside GROUP alt actions resolves correctly.
                cur_fold_var_ = fold_acc;
                w.line("{");
                w.indent();
                w.line("while (true) {");
                w.indent();
                w.line("[[maybe_unused]] size_t rep_pos_ = pos_; [[maybe_unused]] bool rep_la_ = have_la_; [[maybe_unused]] Token rep_tok_ = la_; [[maybe_unused]] uint32_t rep_line_ = line_;");
                w.line("[[maybe_unused]] size_t rep_doc_ = doc_.arena_checkpoint();");
                if (!item.sub_items.empty()) {
                    w.line("{");
                    w.indent();
                    std::string sub_cap = "rep_item_" + id;
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("{} = {};", fold_acc, sub_cap);
                    w.fmt("{} = true;", matched_var);
                    w.line("continue;");
                    w.dedent();
                    w.line("}");
                }
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = rep_pos_; have_la_ = rep_la_; la_ = rep_tok_; line_ = rep_line_;");
                w.line("// arena_rollback suppressed — AST lives until Writ destruction");
                w.line("break;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {};", cap, fold_acc);
                cur_fold_var_.clear();
                if (item.min > 0)
                    w.fmt("if (!{}) goto {};", matched_var, fail_label);
            } else {
                // ── Array-accumulation mode (original behaviour) ─────────────────
                std::string arr_var = "arr_" + id;
                w.fmt("auto {} = doc_.make_array(4).get();", arr_var);
                w.line("{");
                w.indent();
                w.line("while (true) {");
                w.indent();
                w.line("[[maybe_unused]] size_t rep_pos_ = pos_; [[maybe_unused]] bool rep_la_ = have_la_; [[maybe_unused]] Token rep_tok_ = la_; [[maybe_unused]] uint32_t rep_line_ = line_;");
                w.line("[[maybe_unused]] size_t rep_doc_ = doc_.arena_checkpoint();");
                if (!item.sub_items.empty()) {
                    w.line("{");
                    w.indent();
                    std::string sub_cap = "rep_item_" + id;
                    emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
                    w.fmt("if (!{}.is_null()) {}.push_back({}).get();", sub_cap, arr_var, sub_cap);
                    w.line("continue;");
                    w.dedent();
                    w.line("}");
                }
                w.fmt("{}: ;", fail_lbl);
                w.line("pos_ = rep_pos_; have_la_ = rep_la_; la_ = rep_tok_; line_ = rep_line_;");
                w.line("// arena_rollback suppressed — AST lives until Writ destruction");
                w.line("break;");
                w.dedent();
                w.line("}");
                w.dedent();
                w.line("}");
                w.fmt("[[maybe_unused]] AnyVal {} = {}.to_anyval();", cap, arr_var);
                if (item.min > 0)
                    w.fmt("if ({}.size() < {}) goto {};", arr_var, item.min, fail_label);
            }
            break;
        }

        case int32_t(ast::GROUP): {
            // Inline ordered choice: try each alt, use first that succeeds.
            // Each alt: outer scope holds position save; inner scope holds item captures.
            // All labels use unique IDs from fresh() to avoid collisions.
            // If an alt's action uses $..., a per-alt rcap array is set up for it.
            std::string grp_id   = fresh();
            std::string done_lbl = "grp_done_" + grp_id;
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
            w.line("{");
            w.indent();
            w.line("[[maybe_unused]] size_t grp_pos_ = 0; [[maybe_unused]] bool grp_la_ = false; [[maybe_unused]] Token grp_tok_; [[maybe_unused]] size_t grp_doc_ = 0; [[maybe_unused]] uint32_t grp_line_ = 0;");
            for (size_t gi = 0; gi < item.sub_alts.size(); ++gi) {
                const auto& sa = item.sub_alts[gi];
                std::string alt_fail = "grp_fail_" + grp_id + "_" + std::to_string(gi);
                w.fmt("// Group alt {}", gi + 1);
                w.line("{");
                w.indent();
                w.line("grp_pos_ = pos_; grp_la_ = have_la_; grp_tok_ = la_; grp_doc_ = doc_.arena_checkpoint(); grp_line_ = line_;");
                w.line("{"); // inner scope: item captures

                // Per-alt rcap: only when this alt has its own $... action.
                // If no $... action: keep the outer rcap_var_ so that rule refs
                // in this GROUP alt continue to push to the enclosing collector.
                std::string saved_rcap = rcap_var_;
                if (sa.action && action_has_array_capture(*sa.action)) {
                    rcap_var_ = "grp_rcap_" + grp_id + "_" + std::to_string(gi);
                    w.fmt("auto {} = doc_.make_array(4).get();", rcap_var_);
                }

                w.indent();
                // 1-indexed capture slots for this GROUP alt ($1 = first item, etc.).
                std::vector<std::string> sa_caps(1); // sa_caps[0] unused ($0 = FOLD_CAPTURE)
                for (size_t si = 0; si < sa.seq.size(); ++si) {
                    std::string sc = std::format("{}_gi{}_s{}", cap, gi, si);
                    sa_caps.push_back(sc);
                    emit_item_match(w, sa.seq[si], sc, alt_fail, si);
                }
                if (sa.action) {
                    // Emit action: stores result in `cap`; caller emits goto done_lbl.
                    emit_action(w, *sa.action, sa_caps, sa.seq, cap);
                } else if (sa_caps.size() == 2) {
                    w.fmt("{} = {};", cap, sa_caps[1]);   // single-item passthrough
                } else if (size_t lvc = last_value_capture(sa.seq)) {
                    // Same rule as a no-action rule alt: pass through the last
                    // value-carrying capture, not a delimiter token.
                    w.fmt("{} = {};", cap, sa_caps[lvc]);
                } else {
                    w.fmt("{} = AnyVal{{}};  // multi-item group alt (no action)", cap);
                }
                w.fmt("goto {};", done_lbl);
                rcap_var_ = saved_rcap;
                w.dedent();
                w.line("}"); // end inner scope
                w.fmt("{}: ;", alt_fail);
                w.line("pos_ = grp_pos_; have_la_ = grp_la_; la_ = grp_tok_; line_ = grp_line_;");
                w.dedent();
                w.line("}");
            }
            // No alt matched → outer fail.
            w.fmt("goto {};  // no group alternative matched", fail_label);
            w.fmt("{}: ;", done_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::LOOKAHEAD): {
            std::string id       = fresh();
            std::string end_lbl  = "la_end_" + id;
            std::string fail_lbl = "la_fail_" + id;
            w.line("{");
            w.indent();
            w.line("size_t la_pos_ = pos_; bool la_la_ = have_la_; Token la_tok_ = la_; uint32_t la_line_ = line_;");
            w.line("[[maybe_unused]] size_t la_doc_ = doc_.arena_checkpoint();");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "la_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, fail_lbl, 0);
            }
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_; line_ = la_line_;");
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};  // lookahead result (position restored)", cap);
            w.fmt("goto {};", end_lbl);
            w.dedent();
            w.line("}");
            w.fmt("{}: ;", fail_lbl);
            w.line("pos_ = la_pos_; have_la_ = la_la_; la_ = la_tok_; line_ = la_line_;");
            w.fmt("goto {};", fail_label);
            w.fmt("{}: ;", end_lbl);
            w.dedent();
            w.line("}");
            break;
        }

        case int32_t(ast::NEG_AHEAD): {
            std::string id       = fresh();
            std::string ok_lbl   = "na_ok_" + id;
            std::string fail_lbl = "na_fail_" + id;
            w.line("{");
            w.indent();
            w.line("size_t na_pos_ = pos_; bool na_la_ = have_la_; Token na_tok_ = la_; uint32_t na_line_ = line_;");
            w.line("[[maybe_unused]] size_t na_doc_ = doc_.arena_checkpoint();");
            w.line("{");
            w.indent();
            if (!item.sub_items.empty()) {
                std::string sub_cap = "na_" + id;
                emit_item_match(w, item.sub_items[0], sub_cap, ok_lbl, 0);
            }
            // Sub-item matched → negation fails.
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_; line_ = na_line_;");
            w.fmt("goto {};", fail_label);
            w.dedent();
            w.line("}");
            // Label may be dead when sub_items is empty (no goto generated);
            // suppress -Wunused-label.
            w.fmt("__attribute__((unused)) {}: ;", fail_lbl);
            w.fmt("{}: ;", ok_lbl);
            w.line("pos_ = na_pos_; have_la_ = na_la_; la_ = na_tok_; line_ = na_line_;");
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};  // negative lookahead succeeded", cap);
            w.dedent();
            w.line("}");
            break;
        }

        default:
            w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}}; // TODO: item kind {}", cap, item.kind);
            break;
        }
    }

    // ── Action ───────────────────────────────────────────────────────────────
    // out_cap: if empty → emit "return result_" (rule-level alt);
    //          if non-empty → emit "out_cap = result_" (GROUP sub-alt, caller emits goto).

    // ── Cross-grammar EMBED ───────────────────────────────────────────────
    //
    // `where_body <- el::expr` does NOT mean "call el's rule on my token
    // stream". It means: from here, scan the RAW SOURCE to the next structural
    // stop of MY grammar, hand that substring to el's own parser built over MY
    // Writ, and resume at the stop. Sharing the arena is what lets the returned
    // SExpr compose straight into my plan tree. Mirrors peg_gen_logos's
    // emit_embed_call / embed_find_close (codegen.logos:1433-1563).
    bool uses_embed() const {
        for (const auto& r : g_.rules)
            for (const auto& a : r.alts)
                for (const auto& it : a.seq)
                    if (it.kind == int32_t(ast::RULE_REF) && !it.grammar_alias.empty())
                        return true;
        return false;
    }

    // Stop keywords are the IMPORTING grammar's own alpha token literals —
    // `where`, `select`, `having`, … — never the imported grammar's.
    std::vector<std::string> stop_keywords() const {
        std::vector<std::string> out;
        for (const auto& t : g_.tokens) {
            if (t.kind != int32_t(ast::TOKEN_LITERAL)) continue;
            std::string_view p = t.pattern;
            if (p.size() >= 2 && p.front() == '"' && p.back() == '"')
                p = p.substr(1, p.size() - 2);
            if (p.empty()) continue;
            char c0 = p.front();
            if (!(std::isalpha(uint8_t(c0)) || c0 == '_')) continue;
            bool all_word = true;
            for (char c : p) if (!(std::isalnum(uint8_t(c)) || c == '_')) all_word = false;
            if (all_word) out.emplace_back(p);
        }
        return out;
    }

    void emit_embed_runtime(CodeWriter& w) {
        if (!uses_embed()) return;
        auto kws = stop_keywords();
        w.line("// ── Embedded-grammar sub-scan ─────────────────────────────────────────────");
        w.line("// Whole-word match of any of this grammar's keywords at `p`.");
        w.line("static bool embed_at_kw(std::string_view s, size_t p) {");
        w.indent();
        w.line("auto word = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '$'; };");
        w.line("if (p > 0 && word(s[p - 1])) return false;");
        for (const auto& k : kws)
            w.fmt("if (s.compare(p, {0}, \"{1}\") == 0 && (p + {0} >= s.size() || "
                  "!word(s[p + {0}]))) return true;", k.size(), k);
        w.line("return false;");
        w.dedent();
        w.line("}");
        w.line();
        w.line("// First index at or after `pos` where the embedded clause ends.");
        w.line("// `(`/`[` nest; a top-level `)`/`]`/`,`/`;`/`}` or keyword stops.");
        w.line("// `?`/`:` are ternary-balanced so a result-type `:` still stops.");
        w.line("// String literals and `//` comments are skipped WHOLE, so a stop");
        w.line("// character inside them (`select \"a,b\"`) does not end the clause.");
        w.line("static size_t embed_find_close(std::string_view s, size_t pos) {");
        w.indent();
        w.line("size_t depth = 0, tern = 0;");
        w.line("for (size_t p = pos; p < s.size(); ++p) {");
        w.indent();
        w.line("char c = s[p];");
        w.line("if (c == '\"' || c == '\\'') {   // string literal: skip to its close");
        w.indent();
        w.line("char q = c;");
        w.line("for (++p; p < s.size(); ++p) {");
        w.line("    if (s[p] == '\\\\') { ++p; continue; }   // escape: skip the next byte");
        w.line("    if (s[p] == q) break;");
        w.line("}");
        w.line("continue;   // p is at the closing quote (or EOI)");
        w.dedent();
        w.line("}");
        w.line("if (c == '/' && p + 1 < s.size() && s[p + 1] == '/') {");
        w.line("    while (p < s.size() && s[p] != '\\n') ++p;");
        w.line("    continue;");
        w.line("}");
        w.line("if (c == '(' || c == '[') { ++depth; continue; }");
        w.line("if (c == ')' || c == ']') { if (depth == 0) return p; --depth; continue; }");
        w.line("if (depth) continue;");
        w.line("if (c == '?') { ++tern; continue; }");
        w.line("if (c == ':') { if (tern) { --tern; continue; } return p; }");
        w.line("if (c == ',' || c == ';' || c == '}') return p;");
        w.line("if (embed_at_kw(s, p)) return p;");
        w.dedent();
        w.line("}");
        w.line("return s.size();");
        w.dedent();
        w.line("}");
        w.line();
    }

    void emit_embed_call(CodeWriter& w, const Item& item, const std::string& cap,
                         const std::string& fail_label) {
        const ImportRef* imp = nullptr;
        for (const auto& i : g_.imports)
            if (i.alias == item.grammar_alias) imp = &i;
        if (!imp)
            schema_error(std::format("`{}::{}` refers to no %import alias",
                                     item.grammar_alias, item.name));
        if (!imp->arena_ext)
            schema_error(std::format(
                "imported grammar '{}' must declare `arena: external` to be "
                "embedded (its parser has to build into the importer's Writ)",
                imp->alias));
        if (!g_.arena_ext)
            schema_error(std::format(
                "grammar '{}' embeds '{}' but does not itself declare "
                "`arena: external`", g_.name, imp->alias));

        const std::string cls = std::format("{}::{}Parser", imp->ns, to_pascal(imp->name));
        w.fmt("[[maybe_unused]] AnyVal {} = AnyVal{{}};", cap);
        w.line("{");
        w.indent();
        // The embed must start at the next TOKEN, not at pos_: peek_token()
        // lexes one token ahead and leaves pos_ *past* it, so a raw pos_ would
        // start the sub-parse inside the expression.
        w.line("size_t est_ = pos_;");
        w.line("if (have_la_ && la_.text.data()) est_ = size_t(la_.text.data() - source_.data());");
        w.line("have_la_ = false;");
        w.fmt("size_t eend_ = embed_find_close(source_, est_);");
        w.fmt("std::string_view esub_ = source_.substr(est_, eend_ - est_);");
        w.fmt("{} sub_(esub_, doc_);", cls);
        w.fmt("{} = sub_.parse_{}();", cap, item.name);
        // Re-anchor the outer lexer past the consumed sub-range. The token
        // cache is keyed by byte offset, so a plain pos_ move is enough.
        w.line("if (!" + cap + ".is_null()) pos_ = eend_;");
        w.dedent();
        w.line("}");
        w.fmt("if ({}.is_null()) goto {};", cap, fail_label);
    }

    // ── %schema node construction ─────────────────────────────────────────
    //
    // Mirrors peg_gen_logos's emit_schema_action/emit_schema_field
    // (codegen.logos:3545-3795), lowered to raw TOM writes: the Logos backend
    // builds a TYPED struct (`doc.make::<SBin>()`, `node.lhs = …`) and lets
    // logosc map field→key; C++ has no such second pass, so every write is a
    // `put(<explicit key>, …)` taken from the %schema block, and the ADR-0011
    // type code is stamped by hand.
    //
    // Notably: schema nodes get NO SRC_LINE (the numeric dialect always adds
    // one; a schema node has no such declared field).
    [[noreturn]] void schema_error(const std::string& msg) {
        std::fprintf(stderr, "peg_gen: %s: %s\n", g_.name.c_str(), msg.c_str());
        std::exit(1);
    }

    // The C++ expression yielding the raw source text of a captured token —
    // valid exactly when capture_is_token_named() holds for that index.
    static std::string tok_text(const std::string& cap) {
        return "tok_" + cap + "_.text";
    }

    void emit_schema_field(CodeWriter& w, const SchemaDecl& sd,
                           const SchemaField& sf, const ActionField& field,
                           const std::vector<std::string>& captures,
                           const std::vector<Item>& seq) {
        const auto& e = field.expr;
        const std::string arena = "logos::writ::WritAccess::arena(doc_)";
        auto put = [&](const std::string& key, const std::string& val) {
            w.fmt("node->put({}, {}, {}).get();", key, val, arena);
        };
        size_t idx = size_t(e.index);
        bool is_cap  = e.kind == int32_t(ast::CAPTURE) && idx < captures.size()
                       && !captures[idx].empty();
        bool is_fold = e.kind == int32_t(ast::FOLD_CAPTURE) && !cur_fold_var_.empty();
        std::string cv = is_cap ? captures[idx] : (is_fold ? cur_fold_var_ : "");
        bool tok_int = is_cap && capture_is_token_named(seq, idx, "INTEGER");
        bool tok_flt = is_cap && capture_is_token_named(seq, idx, "FLOAT");
        bool tok_str = is_cap && capture_is_token_named(seq, idx, "STRING");

        // ── fan: spread an ARRAY_CAPTURE across the node's slot keys ──
        if (sf.is_fan()) {
            int cap_n = sf.fan_cap();
            if (cap_n <= 0)
                schema_error(std::format(
                    "{}.{}: fan field needs a slot count — spell it "
                    "`\"fan <setter> <maxfn> <cap>\"`", sd.name, sf.name));
            if (!sf.has_key)
                schema_error(std::format(
                    "{}.{}: fan field needs `= <first slot key>`", sd.name, sf.name));
            const SchemaField* len = sd.find("count");
            if (!len || !len->has_key)
                schema_error(std::format(
                    "{}.{}: a fan node must declare its length field "
                    "`count: \"i32\" = <key>`", sd.name, sf.name));
            if (e.kind != int32_t(ast::ARRAY_CAPTURE) || rcap_var_.empty())
                schema_error(std::format(
                    "{}.{}: a fan field must be written from `$...`", sd.name, sf.name));
            // Items past the cap are dropped — same as the Logos backend.
            w.fmt("{{ uint64_t n_ = {0}.size(); if (n_ > {1}u) n_ = {1}u;", rcap_var_, cap_n);
            w.indent();
            w.fmt("for (uint64_t i_ = 0; i_ < n_; ++i_) "
                  "node->put(uint8_t({} + i_), {}.get(i_), {}).get();",
                  sf.key, rcap_var_, arena);
            put(std::format("uint8_t({})", len->key),
                std::format("AnyVal::from_value(int32_t(n_))"));
            w.dedent();
            w.line("}");
            return;
        }

        const std::string key = std::format("uint8_t({})", sf.key);

        // ── ref T: an edge to a child node. In Logos this wraps in WRef<T>;
        //    WRef has no C++ runtime type, so a ref field is a plain ref AnyVal.
        //    Any non-capture expr (incl. the `NULLBASE` sentinel) means "null".
        if (sf.is_ref()) {
            if (is_cap || is_fold) put(key, cv);
            else                   put(key, "AnyVal{}");
            return;
        }

        // ── WAny: a dynamically typed value. The captured token's KIND decides
        //    the encoding, and it is known statically (see capture_is_token_named).
        if (sf.ftype == "WAny") {
            if (tok_int)
                put(key, std::format("doc_.make_int(peg_decode_i64({})).get()", tok_text(cv)));
            else if (tok_flt)
                put(key, std::format("doc_.make_f64(peg_decode_f64({})).get()", tok_text(cv)));
            else if (tok_str)
                put(key, std::format(
                    "doc_.make_string(peg_unquote({})).get().to_anyval()", tok_text(cv)));
            else if (is_cap || is_fold) put(key, cv);
            else if (e.kind == int32_t(ast::STR_LIT))
                put(key, std::format("doc_.make_string(\"{}\").get().to_anyval()", e.value));
            else if (e.kind == int32_t(ast::INT_LIT))
                put(key, std::format("doc_.make_int({}).get()", e.int_val));
            else if (e.kind == int32_t(ast::BOOL_LIT))
                put(key, std::format("AnyVal::from_value(bool({}))", e.int_val ? 1 : 0));
            else put(key, "doc_.make_int(0).get()");
            return;
        }

        // ── str: the capture already IS an interned arena string. But an OPT
        // capture that did not match is NULL, and the schema item declares a
        // `str`, not an `Option<str>` — a null in that slot is ill-typed. The
        // Logos backend goes through `wstr_as_str`, which maps null to "" and
        // interns it; mirror that or the two backends build different trees for
        // every query with an absent optional (caught by the WQL oracle).
        if (sf.ftype == "str") {
            if (e.kind == int32_t(ast::STR_LIT))
                put(key, std::format("doc_.make_string(\"{}\").get().to_anyval()", e.value));
            else if (is_cap || is_fold)
                put(key, std::format(
                    "({0}.is_null() ? doc_.make_string(\"\").get().to_anyval() : {0})", cv));
            else put(key, "doc_.make_string(\"\").get().to_anyval()");
            return;
        }

        // ── bool: from a capture this is an OPT-PRESENCE test, not a decode.
        if (sf.ftype == "bool") {
            if (is_cap || is_fold)
                put(key, std::format("AnyVal::from_value(!{}.is_null())", cv));
            else if (e.kind == int32_t(ast::BOOL_LIT))
                put(key, std::format("AnyVal::from_value(bool({}))", e.int_val ? 1 : 0));
            else put(key, "AnyVal::from_value(false)");
            return;
        }

        // ── scalar (i8..i64 / u8..u64 / i56 / isize / usize) ──
        const std::string cast = scalar_cast(sf.ftype);
        if (e.kind == int32_t(ast::INT_LIT))
            put(key, std::format("AnyVal::from_value({}({}))", cast, e.int_val));
        else if (e.kind == int32_t(ast::BOOL_LIT))
            put(key, std::format("AnyVal::from_value(bool({}))", e.int_val ? 1 : 0));
        else if (tok_flt)
            put(key, std::format("AnyVal::from_value({}(peg_decode_f64({})))", cast, tok_text(cv)));
        else if (tok_int)
            put(key, std::format("AnyVal::from_value({}(peg_decode_i64({})))", cast, tok_text(cv)));
        else if (is_cap && capture_is_opt_token(seq, idx))
            // `INTEGER?` — interned token text, or null when unmatched (→ 0).
            put(key, std::format("AnyVal::from_value({}(peg_decode_i64_any({}, doc_)))",
                                 cast, cv));
        else if (is_cap || is_fold)
            // The Logos backend silently decodes ANY capture's text here (a null
            // capture yielding 0), so a captured AST NODE would be decoded as if
            // it were digits. That is the silent-fallthrough class we are fixing.
            schema_error(std::format(
                "{}.{}: scalar field written from a capture that is neither a "
                "literal INTEGER/FLOAT token nor an optional one — a captured "
                "rule result has no scalar encoding", sd.name, sf.name));
        else
            put(key, std::format("AnyVal::from_value({}(0))", cast));
    }

    // Strip the outer /.../ delimiters of a %tokens regex.
    static std::string_view regex_inner(const std::string& p) {
        std::string_view sv = p;
        if (sv.size() >= 2 && sv.front() == '/' && sv.back() == '/')
            sv = sv.substr(1, sv.size() - 2);
        return sv;
    }

    // A `[...]+` skip pattern → the literal characters it matches, in the order
    // written. Empty for anything richer (ranges, negation, other regex), which
    // the lexer emitter does not model. Order-independent BY CONSTRUCTION: the
    // caller tests membership, so `[ \t\n\r]+` and `[ \t\r\n]+` behave alike.
    static std::string skip_char_class(std::string_view inner) {
        if (inner.size() < 3 || inner.front() != '[') return {};
        size_t close = inner.rfind(']');
        if (close == std::string_view::npos || inner.substr(close + 1) != "+") return {};
        std::string out;
        for (size_t i = 1; i < close; ++i) {
            char c = inner[i];
            if (c == '\\' && i + 1 < close) {
                switch (inner[++i]) {
                    case 't':  out += '\t'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 'f':  out += '\f'; break;
                    case 'v':  out += '\v'; break;
                    case '\\': out += '\\'; break;
                    default:   return {};   // not a plain literal set
                }
            } else if (c == '-' || c == '^') {
                return {};                   // ranges / negation not modelled
            } else {
                out += c;
            }
        }
        return out;
    }

    // Is this %skip pattern one of the three shapes the lexer emitter models?
    //
    // There is NO regex engine here — a %tokens pattern is a HINT for choosing a
    // hand-written matcher — so an unrecognised one must be REFUSED. It used to
    // fall through silently, emitting no matcher: `/[ \t\r\n]*/`, `/\s+/`,
    // `/[\s]+/` and `/[ \t\x0b\r\n]+/` each yielded a parser that skipped no
    // whitespace anywhere, with no diagnostic. That is the bug class that cost
    // a day on el.peg; parsing the character class only narrowed it.
    static bool skip_pattern_recognised(std::string_view inner) {
        if (!skip_char_class(inner).empty()) return true;
        return inner.starts_with("//")   || inner.starts_with("\\/\\/")
            || inner.starts_with("/*")   || inner.starts_with("\\/\\*");
    }

    // A literal char as it must appear inside a C++ '...' literal.
    // (Distinct from the older `escape_char`, an identity stub used by the
    // single-char-token fast path — left alone here to keep that path's output
    // byte-identical; it is itself unsound for `'` and `\`.)
    static std::string char_lit(char c) {
        switch (c) {
            case '\t': return "\\t";
            case '\n': return "\\n";
            case '\r': return "\\r";
            case '\f': return "\\f";
            case '\v': return "\\v";
            case '\\': return "\\\\";
            case '\'': return "\\'";
            default:   return std::string(1, c);
        }
    }

    // Is this a form emit_schema_field actually models? Anything else fell
    // through to scalar_cast's int64_t default, so a typo'd type in the %schema
    // block silently produced a wrong-width write.
    static bool ftype_is_known(const SchemaField& f) {
        if (f.is_fan() || f.is_ref()) return true;
        for (const char* k : {"WAny", "str", "bool",
                              "i8", "i16", "i32", "i56", "i64", "isize",
                              "u8", "u16", "u32", "u64", "usize"})
            if (f.ftype == k) return true;
        return false;
    }

    // Declared width → the C++ integer the AnyVal Pod is narrowed to.
    static std::string scalar_cast(const std::string& ft) {
        if (ft == "u8")  return "uint8_t";
        if (ft == "i8")  return "int8_t";
        if (ft == "u16") return "uint16_t";
        if (ft == "i16") return "int16_t";
        if (ft == "u32") return "uint32_t";
        if (ft == "i32") return "int32_t";
        return "int64_t";   // i56 / i64 / u64 / isize / usize
    }

    void emit_schema_action(CodeWriter& w, const Action& action,
                            const std::vector<std::string>& captures,
                            const std::vector<Item>& seq,
                            const std::string& out_cap) {
        const SchemaDecl* sd = nullptr;
        for (const auto& f : action.fields)
            if (f.name == "CODE" && f.expr.kind == int32_t(ast::STR_LIT))
                sd = g_.find_schema(f.expr.value);
        if (!sd)
            schema_error("schema-mode alt has no `CODE: \"<SchemaType>\"` field "
                         "naming a declared %schema node");
        if (!sd->has_type_code)
            schema_error(std::format("schema node '{}' has no `code(0x…)` type code",
                                     sd->name));

        // Validate every written field exists and is addressable.
        for (const auto& f : action.fields) {
            if (f.name == "CODE") continue;
            const SchemaField* sf = sd->find(f.name);
            if (!sf)
                schema_error(std::format("schema node '{}' has no field '{}' "
                                         "(declare it in the %schema block)",
                                         sd->name, f.name));
            if (!sf->is_fan() && !sf->has_key)
                schema_error(std::format("{}.{}: missing `= KEY`", sd->name, sf->name));
        }

        // Size the node by its DECLARED capacity, not by the fields this action
        // happens to write. A TinyObjectMap's capacity is fixed at construction,
        // and downstream passes (the WQL plan walker stamps fn_name/row_ty, the
        // gpath desugar clears has_gpath) write fields the PARSER never touched.
        // Logos sizes by the schema item's field count; C++ must agree.
        w.fmt("auto* node = logos::writ::WritAccess::raw_tiny_map(doc_, {}).get();",
              sd->cap > 0 ? sd->cap : 1);
        w.fmt("node->set_schema_type_code({}ull);", sd->type_code);

        for (const auto& f : action.fields) {
            if (f.name == "CODE") continue;
            emit_schema_field(w, *sd, *sd->find(f.name), f, captures, seq);
        }

        w.line("{");
        w.indent();
        w.line("AnyVal result_;");
        w.line("result_.set_ref(node);");
        if (out_cap.empty()) w.line("return result_;");
        else                 w.fmt("{} = result_;", out_cap);
        w.dedent();
        w.line("}");
    }

    void emit_action(CodeWriter& w, const Action& action,
                     const std::vector<std::string>& captures,
                     const std::vector<Item>& seq,
                     const std::string& out_cap = "") {
        // Schema mode is a whole-grammar switch — a grammar builds either raw
        // numeric-key TOMs (%fields/%nodes) or typed schema nodes (%schema),
        // never both.
        if (g_.schema_mode()) {
            emit_schema_action(w, action, captures, seq, out_cap);
            return;
        }
        int slot_count = int(action.fields.size()) + 2; // +1 for CODE, +1 for SRC_LINE
        w.fmt("auto* node = logos::writ::WritAccess::raw_tiny_map(doc_, {}).get();", slot_count);

        for (const auto& field : action.fields) {
            const auto& expr = field.expr;
            // Resolve field name: prefer the rule's group, fall back to global.
            std::string field_const;
            bool found_in_group = false;
            if (!cur_rule_group_.empty()) {
                for (const auto& f : g_.fields) {
                    if (f.group == cur_rule_group_ && f.name == field.name) {
                        field_const = ast_ns_ + "::" + cur_rule_group_ + "::" + field.name;
                        found_in_group = true;
                        break;
                    }
                }
            }
            if (!found_in_group)
                field_const = ast_ns_ + "::" + field.name;

            switch (expr.kind) {

            case int32_t(ast::CAPTURE): {
                // All captures are AnyVal — token captures hold intern'd string offsets,
                // rule captures hold arena object offsets.
                size_t idx = size_t(expr.index);
                if (idx < captures.size() && !captures[idx].empty()) {
                    w.fmt("node->put({}, {}, logos::writ::WritAccess::arena(doc_)).get();",
                          field_const, captures[idx]);
                } else {
                    w.fmt("// {} : ${}  — capture index out of range", field.name, idx);
                }
                break;
            }

            case int32_t(ast::FOLD_CAPTURE): {
                // $0 — the fold accumulator: the result of the preceding sequence item.
                if (!cur_fold_var_.empty()) {
                    w.fmt("node->put({}, {}, logos::writ::WritAccess::arena(doc_)).get();",
                          field_const, cur_fold_var_);
                } else {
                    w.fmt("// {} : $0  — no fold context (FOLD_CAPTURE outside fold REP)", field.name);
                }
                break;
            }

            case int32_t(ast::ARRAY_CAPTURE): {
                // $... — use the rule-captures collector built during item matching.
                // rcap_VAR was declared before the items and populated by every RULE_REF.
                // TOKEN_REF captures are NOT included — they're structural punctuation.
                w.fmt("node->put({}, {}.to_anyval(), logos::writ::WritAccess::arena(doc_)).get();",
                      field_const, rcap_var_);
                break;
            }

            case int32_t(ast::STR_LIT): {
                // Symbolic name (e.g. MAP_NODE) → NamedCode value.
                w.fmt("node->put({}, AnyVal::from_value({}::{}), logos::writ::WritAccess::arena(doc_)).get();",
                      field_const, ast_ns_, expr.value);
                // When writing the CODE discriminant, mirror it into the
                // TinyObjectMap header as schema_type_code so runtime dispatch
                // does not need a bitmap+popcount CODE-key lookup.
                if (field.name == "CODE") {
                    w.fmt("node->set_schema_type_code(logos::writ::schema::ast(static_cast<int32_t>({}::{})));",
                          ast_ns_, expr.value);
                }
                break;
            }

            case int32_t(ast::INT_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(int32_t({})), logos::writ::WritAccess::arena(doc_)).get();",
                      field_const, expr.int_val);
                if (field.name == "CODE") {
                    w.fmt("node->set_schema_type_code(logos::writ::schema::ast(int32_t({})));",
                          expr.int_val);
                }
                break;
            }

            case int32_t(ast::BOOL_LIT): {
                w.fmt("node->put({}, AnyVal::from_value(uint8_t({})), logos::writ::WritAccess::arena(doc_)).get();",
                      field_const, expr.int_val);
                break;
            }

            default:
                w.fmt("// TODO: action expr kind {} for field {}", expr.kind, field.name);
                break;
            }
        }
        // Emit SRC_LINE (source line number of the first token — always present).
        w.fmt("node->put({}::SRC_LINE, AnyVal::from_value(first_line_), logos::writ::WritAccess::arena(doc_)).get();",
              ast_ns_);
        w.line("{");
        w.indent();
        w.line("AnyVal result_;");
        w.line("result_.set_ref(node);");
        if (out_cap.empty())
            w.line("return result_;");
        else
            w.fmt("{} = result_;", out_cap);
        w.dedent();
        w.line("}");
    }

    // ── Pratt ─────────────────────────────────────────────────────────────────

    void emit_pratt(CodeWriter& w) {
        w.line("// ── Pratt expression parser ───────────────────────────────────────────────");
        w.line("// Precedence table (low → high):");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            const auto& level = g_.prec[i];
            std::string assoc_str =
                (level.assoc == int32_t(ast::ASSOC_LEFT))  ? "left"  :
                (level.assoc == int32_t(ast::ASSOC_RIGHT)) ? "right" : "none";
            std::string toks;
            for (const auto& t : level.tokens) toks += t + " ";
            w.fmt("//   prec {}  {}  {}", i + 1, assoc_str, toks);
        }
        w.line();

        // Token prec lookup.
        w.fmt("static int {}_token_prec(TK_{} kind) {{", g_.name, to_upper(g_.name));
        w.indent();
        w.line("switch (kind) {");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            for (const auto& t : g_.prec[i].tokens)
                w.fmt("case TK_{}::{}: return {};", to_upper(g_.name), safe_tok_name(t), int(i + 1));
        }
        w.line("default: return 0;");
        w.line("}");
        w.dedent();
        w.line("}");
        w.line();

        // Right-associative check.
        w.fmt("static bool {}_is_right_assoc(TK_{} kind) {{", g_.name, to_upper(g_.name));
        w.indent();
        w.line("switch (kind) {");
        for (size_t i = 0; i < g_.prec.size(); ++i) {
            if (g_.prec[i].assoc == int32_t(ast::ASSOC_RIGHT)) {
                for (const auto& t : g_.prec[i].tokens)
                    w.fmt("case TK_{}::{}: return true;", to_upper(g_.name), safe_tok_name(t));
            }
        }
        w.line("default: return false;");
        w.line("}");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("AnyVal {}::pratt_expr(int min_prec) {{", parser_class_);
        w.indent();
        w.line("AnyVal left = pratt_atom();");
        w.line("if (left.is_null()) return AnyVal{};");
        w.line("while (true) {");
        w.indent();
        w.line("TK t = peek_token().kind;");
        w.fmt("int prec = {}_token_prec(t);", g_.name);
        w.line("if (prec < min_prec || prec == 0) break;");
        w.fmt("int next_min = {}_is_right_assoc(t) ? prec : prec + 1;", g_.name);
        w.line("next_token(); // consume operator");
        w.line("AnyVal right = pratt_expr(next_min);");
        w.line("// TODO: build BINARY_EXPR node for (left op right)");
        w.line("left = right; // placeholder");
        w.dedent();
        w.line("}");
        w.line("return left;");
        w.dedent();
        w.line("}");
        w.line();

        w.fmt("AnyVal {}::pratt_atom() {{", parser_class_);
        w.indent();
        w.line("// TODO: delegate to the lowest-precedence non-infix rule.");
        w.line("return AnyVal{};");
        w.dedent();
        w.line("}");
        w.line();
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    static std::string_view unquote(const std::string& s) {
        std::string_view sv = s;
        if (sv.size() >= 2 && sv.front() == '"' && sv.back() == '"')
            return sv.substr(1, sv.size() - 2);
        return sv;
    }

    static std::string to_upper(const std::string& s) {
        std::string r = s;
        for (char& c : r) c = char(std::toupper(c));
        return r;
    }

    static char escape_char(char c) { return c; }

    static std::string peg_text(const Item::Alt& alt) {
        std::string out;
        for (const auto& item : alt.seq) {
            if (!out.empty()) out += ' ';
            out += item_text(item);
        }
        if (alt.action) out += " => {...}";
        return out;
    }

    // Token names that clash with C macros or C++ keywords — suffix with _KW.
    static std::string safe_tok_name(const std::string& name) {
        static const std::unordered_set<std::string> reserved = {
            "NULL", "TRUE", "FALSE", "EOF", "OVERFLOW", "UNDERFLOW",
            "ERANGE", "EDOM", "ERRNO", "NAN", "INFINITY",
        };
        return reserved.count(name) ? name + "_KW" : name;
    }

    static std::string item_text(const Item& item) {
        switch (item.kind) {
        case int32_t(ast::TOKEN_REF):  return item.name;
        case int32_t(ast::RULE_REF):
            return item.grammar_alias.empty() ? item.name : item.grammar_alias + "::" + item.name;
        case int32_t(ast::LITERAL):    return '"' + item.name + '"';
        case int32_t(ast::OPT):
            return (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "") + "?";
        case int32_t(ast::REP):
            return (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "") +
                   (item.min == 0 ? "*" : "+");
        case int32_t(ast::GROUP):      return "(...)";
        case int32_t(ast::LOOKAHEAD):  return "&" + (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "");
        case int32_t(ast::NEG_AHEAD):  return "!" + (!item.sub_items.empty() ? item_text(item.sub_items[0]) : "");
        default: return "?";
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════
// %schema ↔ ADR-0011 `schema` item cross-check
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct LogosField { std::string name, type; int32_t key = 0; bool has_key = false; };
struct LogosItem  { uint64_t code = 0; bool has_code = false; std::vector<LogosField> fields; };

std::string strip_line_comments(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n') ++i;
            if (i < s.size()) out += '\n';
        } else out += s[i];
    }
    return out;
}

uint64_t parse_num(std::string_view t) {
    std::string b;
    for (char c : t) if (c != '_') b += c;
    int base = 10;
    std::string_view v = b;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) { base = 16; v.remove_prefix(2); }
    uint64_t r = 0;
    std::from_chars(v.data(), v.data() + v.size(), r, base);
    return r;
}

bool ident_char(char c) { return std::isalnum(uint8_t(c)) || c == '_'; }

// Scan `pub schema <Name> : code(<n>) { f: T = K, … }` items. `schema enum` is
// skipped: it is a discriminated union, not a field-bearing node.
std::map<std::string, LogosItem> read_logos_schemas(const std::vector<fs::path>& files,
                                                    std::string& err) {
    std::map<std::string, LogosItem> out;
    for (const auto& f : files) {
        std::ifstream in(f);
        if (!in) { err += std::format("  cannot open {}\n", f.string()); continue; }
        std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        src = strip_line_comments(src);

        const std::string kw = "pub schema ";
        for (size_t p = src.find(kw); p != std::string::npos; p = src.find(kw, p + 1)) {
            size_t i = p + kw.size();
            while (i < src.size() && std::isspace(uint8_t(src[i]))) ++i;
            size_t ns = i;
            while (i < src.size() && ident_char(src[i])) ++i;
            std::string name = src.substr(ns, i - ns);
            if (name == "enum") continue;

            LogosItem item;
            while (i < src.size() && std::isspace(uint8_t(src[i]))) ++i;
            if (i < src.size() && src[i] == ':') {
                size_t lp = src.find('(', i), rp = src.find(')', i);
                if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
                    item.code = parse_num(std::string_view(src).substr(lp + 1, rp - lp - 1));
                    item.has_code = true;
                    i = rp + 1;
                }
            }
            size_t lb = src.find('{', i);
            if (lb == std::string::npos) continue;
            int depth = 0; size_t rb = lb;
            for (; rb < src.size(); ++rb) {
                if (src[rb] == '{') ++depth;
                else if (src[rb] == '}' && --depth == 0) break;
            }
            std::string body = src.substr(lb + 1, rb - lb - 1);

            // Split on top-level commas (angle brackets nest: WRef<SExpr>).
            std::vector<std::string> parts;
            int ang = 0; std::string cur;
            for (char c : body) {
                if (c == '<') ++ang;
                else if (c == '>') --ang;
                if (c == ',' && ang == 0) { parts.push_back(cur); cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) parts.push_back(cur);

            for (auto& part : parts) {
                size_t colon = part.find(':');
                if (colon == std::string::npos) continue;
                size_t eq = part.find('=', colon);
                auto trim = [](std::string s) {
                    size_t a = s.find_first_not_of(" \t\r\n");
                    if (a == std::string::npos) return std::string{};
                    size_t b = s.find_last_not_of(" \t\r\n");
                    return s.substr(a, b - a + 1);
                };
                LogosField lf;
                lf.name = trim(part.substr(0, colon));
                if (eq == std::string::npos) {
                    lf.type = trim(part.substr(colon + 1));
                } else {
                    lf.type = trim(part.substr(colon + 1, eq - colon - 1));
                    lf.key = int32_t(parse_num(trim(part.substr(eq + 1))));
                    lf.has_key = true;
                }
                if (!lf.name.empty()) item.fields.push_back(std::move(lf));
            }
            out[name] = std::move(item);
        }
    }
    return out;
}

// The .logos spelling of a %schema ftype. `ref X` is `WRef<X>`; everything else
// is written the same on both sides.
std::string logos_type_of(const SchemaField& f) {
    if (f.is_ref()) return "WRef<" + f.ref_target() + ">";
    return f.ftype;
}

} // namespace

bool check_schema_mirror(const std::vector<ResolvedModule>& modules,
                         const std::vector<fs::path>& logos_files) {
    std::string err;
    auto items = read_logos_schemas(logos_files, err);
    if (items.empty()) err += "  no `pub schema` items found\n";

    for (const auto& mod : modules) {
        GrammarInfo g = GrammarReader::read(mod.grammar, modules);
        if (!g.schema_mode()) continue;

        for (const auto& sd : g.schemas) {
            auto it = items.find(sd.name);
            if (it == items.end()) {
                err += std::format("  {}: no `pub schema {}` in the .logos sources\n",
                                   sd.name, sd.name);
                continue;
            }
            const LogosItem& li = it->second;
            if (sd.has_type_code && li.has_code && sd.type_code != li.code)
                err += std::format("  {}: code(0x{:X}) in the grammar, 0x{:X} in the schema item\n",
                                   sd.name, sd.type_code, li.code);
            if (sd.has_cap && size_t(sd.cap) != li.fields.size())
                err += std::format("  {}: cap({}) in the grammar, the schema item has {} fields\n",
                                   sd.name, sd.cap, li.fields.size());

            for (const auto& f : sd.fields) {
                if (f.is_fan()) {
                    // A fan owns no field of its own: it writes `count` (the
                    // node's length) and the slots [key, key+cap).
                    int cap = f.fan_cap();
                    int found = 0;
                    for (const auto& lf : li.fields)
                        if (lf.has_key && lf.key >= f.key && lf.key < f.key + cap) ++found;
                    if (found != cap)
                        err += std::format("  {}.{}: fan declares {} slots from key {}, "
                                           "the schema item has {}\n",
                                           sd.name, f.name, cap, f.key, found);
                    continue;
                }
                auto lf = std::find_if(li.fields.begin(), li.fields.end(),
                                       [&](const LogosField& x) { return x.name == f.name; });
                if (lf == li.fields.end()) {
                    err += std::format("  {}.{}: declared in the grammar, absent from the schema item\n",
                                       sd.name, f.name);
                    continue;
                }
                if (f.has_key && lf->has_key && f.key != lf->key)
                    err += std::format("  {}.{}: key {} in the grammar, {} in the schema item\n",
                                       sd.name, f.name, f.key, lf->key);
                std::string want = logos_type_of(f);
                if (want != lf->type)
                    err += std::format("  {}.{}: type \"{}\" in the grammar, `{}` in the schema item\n",
                                       sd.name, f.name, want, lf->type);
            }
        }
    }

    if (err.empty()) return true;
    std::fprintf(stderr,
        "peg_gen: a %%schema block disagrees with the `schema` item it mirrors.\n"
        "The grammar declares only the fields it writes, so it cannot GENERATE the\n"
        "item — but every field it does declare must match, or the generated parser\n"
        "writes the wrong TOM slot.\n%s", err.c_str());
    return false;
}

void codegen(const std::vector<ResolvedModule>& modules, const CodegenOptions& opts) {
    fs::create_directories(opts.output_dir);

    for (const auto& mod : modules) {
        GrammarInfo g = GrammarReader::read(mod.grammar, modules);
        if (g.output.empty()) {
            std::println(stderr, "peg_gen: module '{}' has no %%meta output name — skipping",
                mod.path);
            continue;
        }

        // Validate BEFORE emitting: report every structural mistake at once.
        // (Emission is atomic anyway — see CodeGen::AtomicFile — so a later
        // bail cannot leave a truncated artifact behind.)
        CodeGen::validate_skips(g, mod.path);
        if (g.schema_mode()) CodeGen::validate_schema(g, mod.path);

        std::println("peg_gen: generating {}.hpp / .cpp  ({})",
            g.output, mod.path);

        CodeGen cg(g, opts.output_dir);
        cg.emit_all();

        // Only the ROOT grammar owns the AST constants header (it is the last
        // module the resolver emits — imports come first).
        if (!opts.ast_header.empty() && &mod == &modules.back()) {
            std::println("peg_gen: writing AST constants header {}",
                         opts.ast_header.string());
            cg.emit_ast_header(opts.ast_header, mod.path);
        }
    }
}

} // namespace logos::peg_gen
