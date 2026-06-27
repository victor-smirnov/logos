# ADR 0005 — L-IR Builders and the Fate of `lir::LExpr`/`lir::LStmt`

Status: Accepted 2026-04-26. Implementation: header sketch landed alongside
this ADR (see `include/logos/compiler/lir_builder.hpp`); migration of
`sema_expr.cpp` / `sema_stmt.cpp` follows in subsequent slices.

## Context

After Phase 3e (mlir_gen, borrow_check, mono_clone migrated to view-based
read-paths), the only remaining `std::variant` access in compiler code
lives at the *creation* sites: 58 sites in `sema_expr.cpp` and 81 in
`sema_stmt.cpp` build L-IR by direct variant construction —

```cpp
auto e = std::make_unique<lir::LExpr>();
e->type = ty;
e->kind = lir::EBinOp{op, std::move(lhs), std::move(rhs)};
```

Phase 3 master plan ends with "L-IR is a Writ document, not a parallel
variant tree." Two questions block that endgame:

1. **Migration shape.** Should sema keep emitting `LExpr` variants while
   `lir_mirror.cpp` projects them into Writ (current arrangement),
   should it emit Writ nodes directly with no variant intermediate, or
   should something between?
2. **Consumer signatures.** mlir_gen / borrow_check / mono_clone read via
   `expr_ref_of(*lexpr)` today — the `const lir::LExpr&` parameter is
   load-bearing because it owns the addressable identity that the mirror
   keys on. If variants disappear, those signatures must change.

The cost of getting question 1 wrong is large: a 9 KLOC migration done
twice. ADR pins the shape before sema work begins.

## Decision

### Two-stage migration: dual-write builders → Writ-only cutover.

**Stage 3f (this ADR's primary scope).** Introduce `LirBuilder`, a class
that owns construction of every L-IR node. Each method takes the same
information sema currently passes to the variant constructor and returns
an `LExprPtr` / `LStmtPtr` — *identical* to today's ownership shape.
Internally the builder writes only the variant; the mirror is still
emitted post-sema by `lir_mirror_emit_into`. Sema migrates site-by-site;
each migrated site replaces a `std::make_unique<LExpr>() + assign kind`
sequence with a single `builder.foo(...)` call. Tests stay green
throughout. No variant types are removed in Stage 3f.

**Stage 3g (future, out of scope for this ADR).** Once 100% of sema
creation goes through `LirBuilder`, switch the builder's implementation
to write directly into a Writ zone. Variant types `lir::EBinOp`,
`lir::SLet`, etc. are deleted. Consumer signatures (`gen_expr(const
LExpr&)`, etc.) are mass-renamed to take `ExprRef`/`StmtRef` — already
mechanical because Phase 3e routed all reads through views.
`lir_mirror.cpp` ceases to exist (no source-of-truth to mirror from).

### Why staging, not a single radical cutover

A single-step cutover (delete variants, rewrite consumers, add Writ
builders, migrate sema, all at once) is the obvious "do it right"
answer. Rejected because:

- **Per-slice testability.** Stage 3f migrations are independent of each
  other — one expr family per commit, 916/916 between each. A Stage 3g
  cutover, by definition, breaks every consumer in one go and only
  passes tests after a multi-day push.
- **Risk asymmetry.** If Stage 3g uncovers an unforeseen design issue
  with Writ-zone-only L-IR (e.g. mutability, identity, lifetime),
  Stage 3f's investment is preserved — the sema sites already speak
  builder-API; only the builder's implementation needs to change.
- **Bisectable history.** Bug regressions during the migration period
  are bisectable to the specific expr/stmt family that introduced them,
  not to "the day variants disappeared."

### Why a builder class, not free functions

`LirBuilder` carries dependencies (mainly the `LProgram&` for type-pool
access) instead of threading them through 139 free function calls.
Future Stage 3g moves will need a Writ zone reference too — adding
that to a class member is one diff; adding it to 139 callsites is not.

### Builder API shape

One method per variant. Method name = lowercase variant name minus the
`E`/`S` prefix. Children are passed as `LExprPtr`/`LStmtPtr` rvalues.
Scalar fields (strings, ints, bools, types) by value. Returns the owned
node pointer.

```cpp
class LirBuilder {
public:
    explicit LirBuilder(lir::LProgram& prog) noexcept;

    // Expression leaves
    lir::LExprPtr lit_int (int64_t v, TypeRef ty);
    lir::LExprPtr lit_bool(bool v,    TypeRef ty);
    lir::LExprPtr var_ref (std::string name, TypeRef ty);

    // Composite expressions
    lir::LExprPtr bin_op  (std::string op,
                           lir::LExprPtr lhs,
                           lir::LExprPtr rhs,
                           TypeRef ty);

    // ... grows as sema sites migrate
};
```

No fluent/builder-pattern chaining. Each method maps 1:1 to a single
variant of `lir::LExpr` (or `lir::LStmt`). Naming and arg order are
tied to the variant, not to ergonomics — a mechanical migration target,
not a new API surface for users.

### What's *not* in scope

- Writ-direct emission. Stage 3f keeps variant write-path verbatim.
- Removing `lir::LExpr` / `lir::LStmt`. Both stay until Stage 3g.
- mlir_gen / borrow_check / mono_clone signature changes. They keep
  taking `const LExpr&` and read via `expr_ref_of` (status quo).
- A general fluent or DSL-shaped construction API. The builder is a
  mechanical 1:1 wrapper, no more.

## Consequences

- **Migration progress is measurable.** Count of sema sites still doing
  raw `result->kind = lir::EFoo{...}` decreases monotonically; CI can
  enforce the count never grows.
- **Builder coverage grows lazily.** A builder method exists only after
  at least one sema site needs it. No speculative API surface. (Trade:
  no big "API design" review pass; the API is determined by what sema
  actually does.)
- **Phase 3g design space stays open.** Whether the eventual Writ-only
  L-IR uses `Zone<Mutable>` during sema then freezes (per the master
  plan §"Mutability"), or uses a per-function arena pattern, or
  something else, is not pre-committed by Stage 3f.
- **Mirror lifecycle stays simple.** Stage 3f does not move mirror
  emission earlier or later. Mono still emits `out_.mirror_table`
  function-by-function as it clones (and `in_mirror_` for read-path —
  see commit `3fe5f20`).

## Followups

- **Stage 3f migration tracking.** A short living checklist somewhere
  (`docs/lir-builder-migration.md` or just MEMORY) listing remaining
  sema-creation hot spots, struck through as each lands.
- **Stage 3g re-open.** When sema is 100% builder-driven and all
  consumers are mirror-readers, write ADR 0006 to lock in the
  Writ-zone target, then execute the cutover.
- **Deferred question — string interning.** Sema today copies
  `std::string` for every name field. Writ-zone L-IR is the natural
  point to introduce a per-zone string pool (see master plan open
  question §2). Decide in ADR 0006, not here.
