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
    writ::AnyVal array(const std::vector<writ::AnyVal>& items);
    writ::AnyVal block(const std::vector<writ::AnyVal>& stmts, writ::TinyMapView from, Origin o);
    writ::AnyVal as_block(writ::AnyVal body, writ::TinyMapView from, Origin o);
    writ::AnyVal match_of(writ::AnyVal scrut, writ::AnyVal pat, writ::AnyVal guard,
                          writ::AnyVal then_body, writ::AnyVal else_body,
                          writ::TinyMapView from, Origin o);
    writ::AnyVal let_chain(writ::TinyMapView node, writ::AnyVal then_body,
                           writ::AnyVal else_body, Origin o);

    writ::Writ        doc_;
    std::vector<Diag> diags_;
};

} // namespace logos::compiler::hir
