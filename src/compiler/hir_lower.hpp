#pragma once

// ADR 0030 — the core layer (HIR). A type-free pass, AST → core AST, run on
// every body before sema lowers it. It rewrites the SURFACE forms that rustc
// lowers in AST → HIR into the core forms sema already understands, once, so
// sema never meets the surface forms at all (sema_impl.hpp: the surface-code
// gate). The core AST is the AST's own node format (Writ tiny maps, ast.hpp
// codes and keys): a rewrite builds new nodes in this pass's document and
// SHARES every untouched subtree, so a body with nothing to desugar is
// returned as-is.
//
// Every node the pass synthesizes carries the construct's SRC_SPAN and
// SRC_LINE, and ORIGIN = the construct it came from (Origin below), so a
// consumer can tell desugared code from written code and name the construct
// in a diagnostic.
//
// Desugarings (grow one by one; each one deletes sema's branch for the form):
//   if let P = e [&& g] { A } [else B]   → match e { P [if g] => A, _ => B }
//   if let P1 = e1 && c && … { A } [else B] (let-chain)
//                                        → nested match / if, B at each fall-through
//   while let P = e [&& g] { A }         → loop { match e { P [if g] => A, _ => break } }
//   while let P1 = e1 && … { A }         → loop { <let-chain> …, break at each fall-through }
//   while c { A }                         → loop { if c { A } else { break } }   (rustc's shape)
//   S { x, .. } (a literal's field shorthand) → S { x: x, .. }   (`x` resolves as any path does)
//   `return e` / `break 'l e` / `continue 'l` as an EXPRESSION
//                                        → the block `{ return e; }` (the statement form; a
//                                          block ending in an exit has type `!`)
//   (a, (b, _), ..) = e / [a, b] = e / S { f, g: b } = e   (destructuring assignment)
//                                        → { let (t0, (t1, _), ..) = e; a = t0; b = t1; }
//                                          (rustc's desugaring: each place is ASSIGNED, so its
//                                          old value drops, and the rhs moves in once)
//   format!/print!/println!/eprint!/eprintln!/panic!/write!/writeln!("lit", args…)
//                                        → { let buf = String::new(); let a0 = &(arg0); …;
//                                            let f = Formatter::new(&mut buf); f.write_str(..);
//                                            fmt_display(a0, &mut f); …; __fmt_println(buf.as_str()) }
//                                          (rustc's format_args!: each argument evaluated once,
//                                          borrowed, left to right; a write! sink first)
//   matches!(e, P [if g])                → match e { P [if g] => true, _ => false }
//   dbg!(e)                              → { let t = e; eprintln!("[file:line] e = {:?}", t); t }
//   unreachable!/todo!/unimplemented!(…) → panic!("<fixed message>[: <formatted>]", …)
//   'a: { B }  (LABELED_BLOCK)           → 'a: loop { break 'a { B } }  (a loop that runs once)
//   A `loop` statement ending a block whose breaks carry a value is the block's
//   tail expression (Rust: a block-like expression statement at the end of a
//   block is its value).
//   Labels and loop exits are RESOLVED here, lexically: an unknown label
//   (E0426), a label or loop outside the enclosing closure (E0767 / E0267), an
//   exit outside any loop (E0268), `continue` to a labeled block (E0696) and an
//   unlabeled exit inside a labeled block (E0695) are refused, and the exit
//   keeps its node with ORIGIN = ExitRefused. In a FRAGMENT (text sema parses
//   after the body walk: a user macro's arguments, include!, a spliced quote)
//   the enclosing loops are not known; an exit that reaches the fragment's
//   root is left to sema's own check.
//   The macro's arguments are raw text in the AST (a macro call's operand is a
//   token tree); the pass parses them where they stand, and a refused call
//   keeps its node with ORIGIN = Macro after the diagnostic.

#include <logos/writ/compat.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_set>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace logos::compiler::hir {

// The construct a synthesized node came from (the ORIGIN key's value).
enum class Origin : int64_t {
    User            = 0,   // absent key: written by the user
    IfLet           = 1,
    IfLetNoElse     = 2,   // an `if let` in EXPRESSION position without `else`
    LetChain        = 3,
    LetChainNoElse  = 4,
    WhileLet        = 5,
    WhileLetChain   = 6,
    ExprExit        = 7,   // `return e` / `break 'l e` / `continue 'l` in expression position
    Destructure     = 8,   // destructuring assignment `(a, b) = e` / `[a, b] = e` / `S { a, b } = e`
    While           = 9,   // `while c { A }`
    FieldShorthand  = 10,  // `S { x }` in a struct / variant literal
    Macro           = 11,  // a built-in macro's expansion (format family, matches!, dbg!,
                           // unreachable!); on a FN_MACRO_CALL: refused, diagnostic given
    LabeledBlock    = 12,  // `'a: { B }`
    ExitRefused     = 13,  // a break / continue whose target the pass refused (diagnostic given)
    ExprAssign      = 14,  // an assignment in expression position (`|| x = 5`, `A => s += 1,`)
    For             = 15,  // `for p in e { B }` (ADR 0030 S10 row 1)
    Try             = 16,  // `e?` (ADR 0030 S10 row 2)
    Comprehension   = 17,  // `[v for x in it]` / `{k: v for …}` / `@[…]` / `@{…}` (ADR 0030 S10 row 3)
};

struct Diag {
    uint32_t    line = 0;
    std::string message;
};

class Lowering {
public:
    Lowering();

    // The core form of a body (a BLOCK, or an expression in a const / closure
    // position). `stmt` says whether `node` itself sits in statement position.
    writ::AnyVal lower_body(writ::AnyVal node, bool stmt = false, bool fragment = false);

    // A node code (with its shape) that must not reach sema once its body went
    // through lower_body — the surface-code gate asks this.
    static bool is_surface(writ::TinyMapView n) noexcept;

    // Diagnostics the rewrite itself found (a malformed let-chain, a macro
    // call whose arguments or format string are refused).
    std::vector<Diag>& diags() noexcept { return diags_; }

    // The file being lowered (dbg! prints it).
    void set_file(std::string_view f) { file_ = f; }

    // The path (`pkg::Name`) of the item `#[lang = "…"]` binds — the `for`
    // desugaring names IntoIterator / Iterator / Option by it, whatever the
    // user's scope holds. Empty when the lang item is not declared.
    void set_lang_paths(std::function<std::string(std::string_view)> f) { lang_path_ = std::move(f); }
    // ADR 0030 Q1 row 3: the item a written TYPE name denotes in the body's
    // scope, as a path (`pkg::Name`; `::Name` for the root), or "" for one
    // that is not an item (a primitive, a type parameter, `Self`, unknown).
    void set_type_resolver(std::function<std::string(std::string_view)> f) { type_res_ = std::move(f); }
    // Q1 row 3, values: the item a bare value name denotes in the body's scope,
    // as `<kind>:<path>` (kind: fn, const, static, ctor, variant), or "".
    void set_value_resolver(std::function<std::string(std::string_view)> f) { val_res_ = std::move(f); }
    // A name bound OUTSIDE the body being lowered (a parameter; the caller's
    // locals around a fragment).
    void set_local_probe(std::function<bool(std::string_view)> f) { local_ = std::move(f); }

private:
    enum class Ctx { Stmt, Expr };

    writ::AnyVal lower(writ::AnyVal v, Ctx ctx);
    writ::AnyVal lower_map(writ::AnyVal v, Ctx ctx);
    writ::AnyVal desugar(writ::AnyVal v, Ctx ctx);
    // `for p in e { B }` → `match IntoIterator::into_iter(e) { mut it => [label:] loop {
    // match Iterator::next(&mut it) { Option::Some(p) => B, Option::None => break } } }`.
    writ::AnyVal for_loop(writ::TinyMapView n, writ::AnyVal label);
    writ::AnyVal try_expr(writ::TinyMapView n);
    writ::AnyVal comprehension(writ::TinyMapView n);
    std::string lang_item_path(writ::TinyMapView at, std::string_view l);

    // Builders: every node takes `from`'s position and the given origin.
    writ::AnyVal node(int32_t code, writ::TinyMapView from, Origin o,
                      std::initializer_list<std::pair<uint8_t, writ::AnyVal>> keys);
    // `from` with its CODE replaced (every other key kept) and ORIGIN set.
    writ::AnyVal recoded(writ::TinyMapView from, int32_t code, Origin o);
    writ::AnyVal array(const std::vector<writ::AnyVal>& items);
    writ::AnyVal block(const std::vector<writ::AnyVal>& stmts, writ::TinyMapView from, Origin o);
    writ::AnyVal as_block(writ::AnyVal body, writ::TinyMapView from, Origin o);
    writ::AnyVal match_of(writ::AnyVal scrut, writ::AnyVal pat, writ::AnyVal guard,
                          writ::AnyVal then_body, writ::AnyVal else_body,
                          writ::TinyMapView from, Origin o);
    writ::AnyVal let_chain(writ::TinyMapView node, writ::AnyVal then_body,
                           writ::AnyVal else_body, Origin o);
    // Destructuring assignment: the binding list as a let pattern with fresh
    // names, collecting (place, fresh) pairs.
    writ::AnyVal destructure(writ::TinyMapView n);
    writ::AnyVal bind_pattern(writ::AnyVal b, writ::TinyMapView at,
                              std::vector<std::pair<writ::AnyVal, std::string>>& assigns);
    writ::AnyVal str(std::string_view s);
    // Every name a pattern, `let`, parameter or nested fn of `v` binds (an
    // identifier pattern naming a const, a struct or a variant is a path, as
    // in Rust). A value path whose name is bound anywhere in the body carries
    // no RES: the binding may shadow the item.
    void collect_binders(writ::AnyVal v);
    writ::AnyVal with_res(writ::AnyVal cur, std::string_view res);
    std::unordered_set<std::string> bound_;
    writ::AnyVal list_map(const std::vector<writ::AnyVal>& items);   // `{ITEMS: [...]}`
    uint64_t fresh_ = 0;

    // Loop scopes (every enclosing loop / labeled block, innermost last) and
    // the closure / nested-fn barriers over them.
    struct LoopScope { std::string label; bool is_block = false; bool valued = false; };
    struct Barrier   { size_t depth = 0; bool closure = false; };
    std::vector<LoopScope> loops_;
    std::vector<Barrier>   barriers_;
    bool                   labeled_body_ = false;   // the next loop node is a LABELED_LOOP's body
    bool                   fragment_ = false;
    std::vector<const void*> valued_loops_;         // loop nodes a break-with-value targets
    // Resolve a break / continue (statement or expression form); returns the
    // node, recoded with ORIGIN ExitRefused after a refusal.
    writ::AnyVal resolve_exit(writ::AnyVal v);
    writ::AnyVal loop_as_expr(writ::AnyVal v);

    // Built-in macros. `expand_macro` returns `v` for a macro it does not own.
    enum class ArgsEntry { Args, Matches };
    writ::AnyVal expand_macro(writ::AnyVal v);
    // The call's raw argument text parsed where it stands, through this pass;
    // null after nothing parsed (the caller refuses).
    writ::TinyMapView parse_args(writ::TinyMapView call, ArgsEntry entry, bool& ok);
    writ::AnyVal format_expansion(writ::TinyMapView at, std::string_view callee,
                                  std::string_view body, const std::vector<writ::AnyVal>& args,
                                  size_t fmt_pos, bool write_family);
    writ::AnyVal refuse(writ::TinyMapView call, std::string msg);
    std::deque<std::shared_ptr<std::string>> arg_texts_;   // the parsed documents view these
    std::deque<writ::Writ>                   arg_docs_;
    std::string                              file_;
    std::function<std::string(std::string_view)> lang_path_;
    std::function<std::string(std::string_view)> type_res_;
    std::function<std::string(std::string_view)> val_res_;
    std::function<bool(std::string_view)>        local_;

    writ::Writ        doc_;
    std::vector<Diag> diags_;
};

} // namespace logos::compiler::hir
