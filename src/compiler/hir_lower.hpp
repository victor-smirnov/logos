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
//   `return e` / `break 'l e` / `continue 'l` as an EXPRESSION
//                                        → the block `{ return e; }` (the statement form; a
//                                          block ending in an exit has type `!`)
//   (a, (b, _), ..) = e / [a, b] = e / S { f, g: b } = e   (destructuring assignment)
//                                        → { let (t0, (t1, _), ..) = e; a = t0; b = t1; }
//                                          (rustc's desugaring: each place is ASSIGNED, so its
//                                          old value drops, and the rhs moves in once)

#include <logos/writ/compat.hpp>

#include <cstdint>
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
    writ::AnyVal lower_body(writ::AnyVal node, bool stmt = false);

    // A node code (with its shape) that must not reach sema once its body went
    // through lower_body — the surface-code gate asks this.
    static bool is_surface(writ::TinyMapView n) noexcept;

    // Diagnostics the rewrite itself found (none yet; kept for forms whose
    // refusal belongs to the desugaring, e.g. a malformed let-chain).
    std::vector<Diag>& diags() noexcept { return diags_; }

private:
    enum class Ctx { Stmt, Expr };

    writ::AnyVal lower(writ::AnyVal v, Ctx ctx);
    writ::AnyVal lower_map(writ::AnyVal v, Ctx ctx);
    writ::AnyVal desugar(writ::AnyVal v, Ctx ctx);

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
    writ::AnyVal list_map(const std::vector<writ::AnyVal>& items);   // `{ITEMS: [...]}`
    uint64_t fresh_ = 0;

    writ::Writ        doc_;
    std::vector<Diag> diags_;
};

} // namespace logos::compiler::hir
